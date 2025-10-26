#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <ctype.h>
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

// memory for anonymous labels
anonymouslabel_t an_prev;
anonymouslabel_t an_next;
label_t an_return;

// tables
label_t* globalLabelTable[GLOBAL_LABEL_TABLE_SIZE]; // hash table
uint16_t globalLabelCounter;

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

uint16_t getGlobalLabelCount(void) {
    return globalLabelCounter;
}

void initGlobalLabelTable(void) {
    labelmemsize = 0;
    globalLabelCounter = 0;
    labelcollisions = 0;
    memset(globalLabelTable, 0, sizeof(globalLabelTable));
}

void initAnonymousLabelTable(void) {
    an_prev.defined = false;
    an_next.defined = false;
    an_return.name = NULL;
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

void writeAnonymousLabel(uint24_t labelAddress) {
    uint8_t scope;

    scope = contentlevel;
    fwrite((char*)&labelAddress, sizeof(labelAddress), 1, filehandle[FILE_ANONYMOUS_LABELS]);
    fwrite((char*)&scope, sizeof(scope), 1, filehandle[FILE_ANONYMOUS_LABELS]);
    fflush(filehandle[FILE_ANONYMOUS_LABELS]);
}

void readAnonymousLabel(void) {
    uint24_t labelAddress;
    uint8_t scope;

    if(fread((char*)&labelAddress, sizeof(labelAddress), 1, filehandle[FILE_ANONYMOUS_LABELS])) {
        fread((char*)&scope, sizeof(scope), 1, filehandle[FILE_ANONYMOUS_LABELS]);
        if(an_next.defined) {
            an_prev.address = an_next.address;
            an_prev.scope = an_next.scope;
            an_prev.defined = true;
        }
        an_next.address = labelAddress;
        an_next.scope = scope;            
        an_next.defined = true;
    }
    else { // last label already read
        an_prev.address = an_next.address;
        an_prev.scope = an_next.scope;
        an_prev.defined = true;
        an_next.defined = false;
    }
}

bool insertLabel(const char *labelname, uint8_t len, uint24_t labelAddress, bool local){
    uint8_t index;
    label_t *tmp,*try;

    // allocate space in buffer for label_t struct
    tmp = (label_t *)allocateMemory(sizeof(label_t), &labelmemsize);
    if(tmp == NULL) return false;

    // allocate space in buffer for string and store it to buffer
    tmp->name = (char*)allocateMemory(len+1, &labelmemsize);
    if(tmp->name == NULL) return false;

    strcpy(tmp->name, labelname);
    tmp->local = local;
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
        if(((tolower(name[1]) == 'f') || (tolower(name[1]) == 'n')) && name[2] == 0) {
            if(an_next.defined && an_next.scope == contentlevel) {
                an_return.address = an_next.address;
                return &an_return;
            }
            else return NULL;
        }
        if(((tolower(name[1]) == 'b') || (tolower(name[1]) == 'p')) && name[2] == 0) {
            if(an_prev.defined && an_prev.scope == contentlevel) {
                an_return.address = an_prev.address;
                return &an_return;
            }
            else return NULL;
        }
        return findLocalLabel(name);
    }
    else return findGlobalLabel(name);
}

void advanceAnonymousLabel(void) {
    if(currentline.label) {
        if(currentline.label[0] == '@') {
            if(currentline.label[1] == '@') {
                readAnonymousLabel();
            }
        }
    }
}

void definelabel(uint24_t num){
    uint8_t len;

    if(pass == STARTPASS) {
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
    if(currentline.label && currentline.label[0] != '@') {
        strcpy(currentcontentitem->labelscope, currentline.label);
    }
}