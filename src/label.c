#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <ctype.h>
#include "ctype_tab.h"
#include "config.h"
#include "defines.h"
#include "label.h"
#include "hash.h"
#include "str2num.h"
#include "utils.h"
#include "globals.h"
#include "io.h"
#include "macro.h"
#include "assemble.h"

// Total allocated memory for labels
uint24_t labelmemsize;

// Anonymous labels use stable nodes, so forward references need no disk file.
typedef struct anonymousnode {
    struct anonymousnode *next;
    anonymouslabel_t label;
} anonymousnode_t;
static anonymousnode_t anonymousRoot, *anonymousCurrent;
static label_t an_return;

void *anonymousPosition(void) { return anonymousCurrent; }
void restoreAnonymousPosition(void *position) { anonymousCurrent = position; }

static anonymousnode_t *nextAnonymous(void) {
    if(!anonymousCurrent->next) {
        anonymousCurrent->next = allocateMemory(sizeof(anonymousnode_t), &labelmemsize);
        if(anonymousCurrent->next) memset(anonymousCurrent->next, 0, sizeof(anonymousnode_t));
    }
    return anonymousCurrent->next;
}

// tables
label_t* globalLabelTable[GLOBAL_LABEL_TABLE_SIZE]; // hash table
uint24_t globalLabelCounter;

void saveGlobalLabelTable(void) {
    int i;
    char *ptr;
    label_t *lbl;
    FILE *fh;
    char buffer[LINEMAX+1];
    char filename[FILENAMEMAXLENGTH + 1];

    strcpy(filename, filebasename);
    strcat(filename, ".symbols");

    fh = fopen(filename, "wb+");
    if(fh == 0) {
        error(message[ERROR_FILEGLOBALLABELS],0);
        return;
    }

    for(i = 0; i < GLOBAL_LABEL_TABLE_SIZE; i++) {
        if(globalLabelTable[i]) {
            lbl = globalLabelTable[i];
            while(lbl) {
                if(!lbl->local) sprintf(buffer, "%s $%x\r\n", lbl->name, lbl->address);
                ptr = buffer;
                while(*ptr) fputc(*ptr++, fh);
                lbl = lbl->next;
            }
        }
    }
    fclose(fh);
}

uint24_t getGlobalLabelCount(void) {
    return globalLabelCounter;
}

void initGlobalLabelTable(void) {
    labelmemsize = 0;
    globalLabelCounter = 0;
    labelcollisions = 0;
    memset(globalLabelTable, 0, sizeof(globalLabelTable));
}

void initAnonymousLabelTable(void) {
    memset(&anonymousRoot, 0, sizeof(anonymousRoot));
    anonymousCurrent = &anonymousRoot;
    an_return.name = NULL;
    an_return.defined = true;
}

label_t * findLocalLabel(const char *key) {
    char compoundname[(MAXNAMELENGTH * 2)+1];
    char *scopename;
    contentitem_t *ci = currentcontentitem;

    if(currentcontentitem->labelscope[0] == 0) {
        scopename = ci->name; // local to file label
    }
    else {
        scopename = ci->labelscope; // local to global label
    }

    if(currentExpandedMacro) {
        snprintf(compoundname, (MAXNAMELENGTH*2)+1, "%X%s%s", currentExpandedMacro->currentExpandID, scopename, key);
    }
    else strcompound(compoundname, scopename, key);
    return findGlobalLabel(compoundname);
}

/* Anonymous labels have a separate position-dependent representation. */
label_t *internLabel(const char *name) {
    char compound[(MAXNAMELENGTH * 2)+1];
    const char *key = name;
    label_t *node;
    uint8_t index;
    if(name[0] == '@') {
        const char *scope = currentcontentitem->labelscope[0] ? currentcontentitem->labelscope : currentcontentitem->name;
        if(currentExpandedMacro)
            snprintf(compound, sizeof(compound), "%X%s%s", currentExpandedMacro->currentExpandID, scope, name);
        else strcompound(compound, scope, name);
        key = compound;
    }
    node = findGlobalLabel(key);
    if(node) return node;
    node = allocateMemory(sizeof(*node), &labelmemsize);
    if(!node) return NULL;
    node->name = allocateString(key, &labelmemsize);
    if(!node->name) { free(node); labelmemsize -= sizeof(*node); return NULL; }
    node->local = name[0] == '@';
    node->defined = false;
    node->address = 0;
    index = hash256(key);
    node->next = globalLabelTable[index];
    globalLabelTable[index] = node;
    return node;
}

void writeAnonymousLabel(uint24_t labelAddress) {
    anonymousCurrent->label.address = labelAddress;
    anonymousCurrent->label.scope = contentlevel;
    anonymousCurrent->label.defined = true;
}

bool insertLabel(const char *labelname, uint8_t len, uint24_t labelAddress, bool local){
    uint8_t index;
    label_t *tmp,*try;

    // Forward references keep this record stable until its definition.
    tmp = findGlobalLabel(labelname);
    if(tmp) {
        if(tmp->defined) { error(message[ERROR_LABELDEFINED], "%s", labelname); return false; }
        tmp->address = labelAddress;
        tmp->local = local;
        tmp->defined = true;
        globalLabelCounter++;
        return true;
    }

    // allocate space in buffer for label_t struct
    tmp = (label_t *)allocateMemory(sizeof(label_t), &labelmemsize);
    if(tmp == NULL) return false;

    // allocate space in buffer for string and store it to buffer
    tmp->name = (char*)allocateMemory(len+1, &labelmemsize);
    if(tmp->name == NULL) return false;

    strcpy(tmp->name, labelname);
    tmp->local = local;
    tmp->defined = true;
    tmp->address = labelAddress;
    tmp->next = NULL;

    index = hash256(labelname);
    try = globalLabelTable[index];

    // First item on index
    if(try == NULL) {
        globalLabelTable[index] = tmp;
        globalLabelCounter++;
        return true;
    }

    // Collision on index, place at end of linked list if unique
    while(true) {
        if(strcmp(try->name, labelname) == 0) {
            error(message[ERROR_LABELDEFINED],"%s",labelname);
            return false;
        }
        labelcollisions++;
        if(try->next) {
            try = try->next;
        }
        else {
            try->next = tmp;
            globalLabelCounter++;
            return true;
        }
    }
}

bool insertLocalLabel(const char *labelname, uint24_t labelAddress) {
    char compoundname[(MAXNAMELENGTH * 2)+1];
    char *scopename;
    uint8_t len;
    contentitem_t *ci = currentcontentitem;

    if(currentcontentitem->labelscope[0] == 0) {
        scopename = ci->name; // local to file label
    }
    else {
        scopename = ci->labelscope; // local to global label
    }
    if(currentExpandedMacro) {
        snprintf(compoundname, (MAXNAMELENGTH*2)+1, "%X%s%s", currentExpandedMacro->currentExpandID, scopename, labelname);
        len = strlen(compoundname);
    }
    else len = strcompound(compoundname, scopename, labelname);
    return insertLabel(compoundname, len, labelAddress, true);
}

label_t *findGlobalLabel(const char *name){
    uint8_t index;
    label_t *try;

    index = hash256(name);
    try = globalLabelTable[index];

    while(true)
    {
        if(try == NULL) return NULL;
        if(strcmp(try->name, name) == 0) return try;
        try = try->next;
    }
}

label_t *findLabel(const char *name) {
    if(name[0] == '@') {
        if(((TOLOWER(name[1]) == 'f') || (TOLOWER(name[1]) == 'n')) && name[2] == 0) {
            anonymousnode_t *node = nextAnonymous();
            if(node && node->label.defined && node->label.scope == contentlevel) {
                an_return.address = node->label.address;
                return &an_return;
            }
            return NULL;
        }
        if(((TOLOWER(name[1]) == 'b') || (TOLOWER(name[1]) == 'p')) && name[2] == 0) {
            if(anonymousCurrent->label.defined && anonymousCurrent->label.scope == contentlevel) {
                an_return.address = anonymousCurrent->label.address;
                return &an_return;
            }
            return NULL;
        }
        return findLocalLabel(name);
    }
    else return findGlobalLabel(name);
}

void advanceAnonymousLabel(void) {
    if(currentline.label) {
        if(currentline.label[0] == '@') {
            if(currentline.label[1] == '@') {
                if(inConditionalSection != CONDITIONSTATE_FALSE) {
                    anonymousnode_t *node = nextAnonymous();
                    if(node) anonymousCurrent = node;
                }
            }
        }
    }
}

void definelabel(uint24_t num){
    uint8_t len;

    if(currentline.label == NULL) return;

    if(strlen(currentline.label) > MAXNAMELENGTH) {
        error(message[ERROR_LABELTOOLONG], "%s", currentline.label);
        return;
    }

    if(relocate) num = relocateBaseAddress + (num - relocateOutputBaseAddress);
    if(currentline.label[0] == '@') {
        if(currentline.label[1] == '@') {
            if(currentExpandedMacro) {
                error(message[ERROR_MACRO_NOANONYMOUSLABELS],0);
                return;
            }
            writeAnonymousLabel(num);
            return;
        }
        if(insertLocalLabel(currentline.label, num) == false) {
            error(message[ERROR_CREATINGLABEL],0);
            return;
        }
        return;
    }
    if(currentline.label[0] == '$') {
        error(message[ERROR_INVALIDLABEL],"%s",currentline.label);
        return;
    }
    if(currentExpandedMacro) {
        error(message[ERROR_MACRO_NOGLOBALLABELS],0);
        return;
    }
    len = strlen(currentline.label);
    str2num(currentline.label, len);
    if(!err_str2num) { // labels can't have a valid number format
        error(message[ERROR_INVALIDLABEL],"%s",currentline.label);
        return;
    }
    if(insertLabel(currentline.label, len, num, false) == false){
        error(message[ERROR_CREATINGLABEL],0);
        return;
    }

    if(currentline.label) {
        strcpy(currentcontentitem->labelscope, currentline.label);
    }

    return;
}