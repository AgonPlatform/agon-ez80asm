#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "fixup.h"
#include "globals.h"
#include "utils.h"
#include "label.h"
#include "io.h"

/* Only forward expressions allocate records. Repeated data has one record.
 * Source buffers are never retained by a fixup. Context restores $, local and
 * macro scopes, anonymous-label position, and the original diagnostic location.
 */
struct fixup {
    fixup_t *next;
    contentitem_t *source;
    macro_t *macro;
    void *anonymous;
    uint24_t pc, offset, count, expansion;
    unsigned int line, macroline;
    uint8_t depth, kind, opcode;
    bool negative, attached;
    char text[1];                 /* scope\0expression\0, allocated to fit */
};
fixup_t *lastFixup;
bool expressionUnknown, resolvingFixups;
uint24_t fixupmemsize, fixupmempeak;
static fixup_t *first, *last;

fixup_t *captureFixup(const char *expression) {
    size_t size = sizeof(fixup_t) + strlen(currentcontentitem->labelscope) + strlen(expression) + 1;
    fixup_t *f = allocateMemory(size, &fixupmemsize);
    if(!f) return NULL;
    memset(f, 0, sizeof(*f));
    f->source = currentcontentitem;
    f->macro = currentExpandedMacro;
    f->expansion = f->macro ? f->macro->currentExpandID : 0;
    f->anonymous = anonymousPosition();
    f->pc = relocate ? relocateBaseAddress + address - relocateOutputBaseAddress : address;
    f->line = currentcontentitem->currentlinenumber;
    f->macroline = macrolinenumber;
    f->depth = contentlevel;
    strcpy(f->text, currentcontentitem->labelscope);
    strcpy(f->text + strlen(f->text) + 1, expression);
    if(last) last->next = f;
    else first = f;
    last = f;
    if(fixupmemsize > fixupmempeak) fixupmempeak = fixupmemsize;
    return f;
}

void negateFixup(fixup_t *f) { if(f) f->negative = true; }

void attachFixup(fixup_t *f, uint8_t kind, uint24_t count, uint8_t opcode) {
    if(!f) return;
    f->offset = ioOutputPosition() + remaining_dsspaces;
    f->kind = kind;
    f->count = count;
    f->opcode = opcode;
    f->attached = true;
}

void resolveFixups(void) {
    fixup_t *f;
    uint24_t savedAddress = address;
    bool savedRelocate = relocate;
    resolvingFixups = true;
    relocate = false; // pc already contains the relocated logical address
    for(f = first; f && !errorcount; f = f->next) {
        char expression[MACROLINEMAX + 1];
        int32_t value;
        uint24_t offset, count;
        uint8_t width, i;
        currentcontentitem = f->source;
        currentcontentitem->currentlinenumber = f->line;
        strcpy(currentcontentitem->labelscope, f->text);
        currentExpandedMacro = f->macro;
        if(f->macro) f->macro->currentExpandID = f->expansion;
        macrolinenumber = f->macroline;
        contentlevel = f->depth;
        restoreAnonymousPosition(f->anonymous);
        address = f->pc;
        strcpy(expression, f->text + strlen(f->text) + 1);
        value = getExpressionValue(expression, REQUIRED_NOW);
        if(errorcount) break;
        if(f->negative) value = -(int16_t)value;
        if(!f->attached) continue;
        width = f->kind;
        switch(f->kind) {
            case FIX_DS:
                if(f->count && value != f->opcode)
                    warning(message[WARNING_UNSUPPORTED_INITIALIZER], "%s", expression);
                continue;
            case 1: validateRange8bit(&value, expression); break;
            case 2: validateRange16bit(&value, expression); break;
            case 3: validateRange24bit(&value, expression); break;
            case 4: break;
            case FIX_REL:
                value -= f->pc + 2;
                if(value < -128 || value > 127) error(message[ERROR_RELATIVEJUMPTOOLARGE], 0);
                width = 1;
                break;
            case FIX_DISP:
                value = (int16_t)value;
                if(value < -128 || value > 127) error(message[ERROR_DISPLACEMENT_RANGE], "%d", value);
                width = 1;
                break;
            case FIX_BIT:
                if(value < 0 || value > 7) { error(message[ERROR_INVALIDBITNUMBER], "%s", expression); break; }
                value = f->opcode | (value << 3);
                width = 1;
                break;
            case FIX_RST:
                if(value < 0 || value > 0x38 || (value & 7)) error(message[ERROR_ILLEGALRESTARTADDRESS], "%s", expression);
                value |= f->opcode;
                width = 1;
                break;
            case FIX_IM:
                if(value < 0 || value > 2) { error(message[ERROR_ILLEGALINTERRUPTMODE], "%s", expression); break; }
                value = f->opcode | ((value ? value + 1 : 0) << 3);
                width = 1;
                break;
        }
        if(errorcount) break;
        offset = f->offset;
        count = f->count;
        while(count-- && !errorcount) {
            for(i = 0; i < width; i++) ioPatchByte(offset++, (uint32_t)value >> (8*i));
        }
    }
    address = savedAddress;
    relocate = savedRelocate;
    currentcontentitem = NULL;
    currentExpandedMacro = NULL;
    contentlevel = 0;
    resolvingFixups = false;
}

void freeFixups(void) {
    while(first) {
        fixup_t *next = first->next;
        free(first);
        first = next;
    }
    last = lastFixup = NULL;
    fixupmemsize = 0;
}
