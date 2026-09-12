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
    union { void *anonymous; label_t *symbol; } target;
    bool simple;
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

static fixup_t *capture(const char *expression, label_t *symbol) {
    size_t size = sizeof(fixup_t) + (symbol ? 0 : strlen(currentcontentitem->labelscope)) + strlen(expression) + 1;
    fixup_t *f = allocateMemory(size, &fixupmemsize);
    if(!f) return NULL;
    memset(f, 0, sizeof(*f));
    f->source = currentcontentitem;
    f->macro = currentExpandedMacro;
    f->expansion = f->macro ? f->macro->currentExpandID : 0;
    f->simple = symbol != NULL;
    if(symbol) f->target.symbol = symbol;
    else f->target.anonymous = anonymousPosition();
    f->pc = relocate ? relocateBaseAddress + address - relocateOutputBaseAddress : address;
    f->line = currentcontentitem->currentlinenumber;
    f->macroline = macrolinenumber;
    f->depth = contentlevel;
    if(!symbol) strcpy(f->text, currentcontentitem->labelscope);
    strcpy(f->text + strlen(f->text) + 1, expression);
    if(last) last->next = f;
    else first = f;
    last = f;
    if(fixupmemsize > fixupmempeak) fixupmempeak = fixupmemsize;
    return f;
}

fixup_t *captureFixup(const char *expression) { return capture(expression, NULL); }
fixup_t *captureSymbolFixup(label_t *symbol, const char *name) { return capture(name, symbol); }

void negateFixup(fixup_t *f) { if(f) f->negative = true; }

void attachFixup(fixup_t *f, uint8_t kind, uint24_t count, uint8_t opcode) {
    attachFixupAt(f, kind, count, opcode, ioOutputPosition() + remaining_dsspaces);
}

void attachFixupAt(fixup_t *f, uint8_t kind, uint24_t count, uint8_t opcode, uint24_t offset) {
    if(!f) return;
    f->offset = offset;
    f->kind = kind;
    f->count = count;
    f->opcode = opcode;
    f->attached = true;
}

static void restoreContext(const fixup_t *f) {
    currentcontentitem = f->source;
    currentcontentitem->currentlinenumber = f->line;
    strcpy(currentcontentitem->labelscope, f->text);
    currentExpandedMacro = f->macro;
    if(f->macro) f->macro->currentExpandID = f->expansion;
    macrolinenumber = f->macroline;
    contentlevel = f->depth;
    if(!f->simple) restoreAnonymousPosition(f->target.anonymous);
    address = f->pc;
}

#if defined(__GNUC__)
__attribute__((noinline))
#endif
static int32_t resolveExpression(fixup_t *f) {
    char expression[MACROLINEMAX + 1];
    restoreContext(f);
    strcpy(expression, f->text + strlen(f->text) + 1);
    return getExpressionValue(expression, REQUIRED_NOW);
}

void resolveFixups(void) {
    fixup_t *f;
    uint24_t savedAddress = address;
    bool savedRelocate = relocate;
    resolvingFixups = true;
    relocate = false; // pc already contains the relocated logical address
    for(f = first; f && !errorcount; f = f->next) {
        const char *expression;
        int32_t value;
        uint24_t offset, count;
        uint8_t width;
        expression = f->text + strlen(f->text) + 1;
        if(f->simple) {
            if(!f->target.symbol->defined) {
                restoreContext(f);
                error(message[ERROR_IDENTIFIER], "%s", expression);
                break;
            }
            value = f->target.symbol->address;
        }
        else value = resolveExpression(f);
        if(errorcount) break;
        if(f->negative) value = -(int16_t)value;
        if(!f->attached) continue;
        width = f->kind;
#define FIX_ERROR(...) do { if(f->simple) restoreContext(f); error(__VA_ARGS__); } while(0)
#define FIX_WARNING(...) do { if(f->simple) restoreContext(f); warning(__VA_ARGS__); } while(0)
        switch(f->kind) {
            case FIX_DS:
                if(f->count && value != f->opcode)
                    FIX_WARNING(message[WARNING_UNSUPPORTED_INITIALIZER], "%s", expression);
                continue;
            case 1:
                if(!ignore_truncation_warnings && OUTOFRANGE8(&value)) {
                    if(f->simple) restoreContext(f);
                    validateRange8bit(&value, expression);
                }
                break;
            case 2:
                if(!ignore_truncation_warnings && OUTOFRANGE16(&value)) {
                    if(f->simple) restoreContext(f);
                    validateRange16bit(&value, expression);
                }
                break;
            case 3:
                if(!ignore_truncation_warnings && OUTOFRANGE24(&value)) {
                    if(f->simple) restoreContext(f);
                    validateRange24bit(&value, expression);
                }
                break;
            case 4: break;
            case FIX_REL:
                value -= f->pc + 2;
                if(value < -128 || value > 127) FIX_ERROR(message[ERROR_RELATIVEJUMPTOOLARGE], 0);
                width = 1;
                break;
            case FIX_DISP:
                value = (int16_t)value;
                if(value < -128 || value > 127) FIX_ERROR(message[ERROR_DISPLACEMENT_RANGE], "%d", value);
                width = 1;
                break;
            case FIX_BIT:
                if(value < 0 || value > 7) { FIX_ERROR(message[ERROR_INVALIDBITNUMBER], "%s", expression); break; }
                value = f->opcode | (value << 3);
                width = 1;
                break;
            case FIX_RST:
                if(value < 0 || value > 0x38 || (value & 7)) FIX_ERROR(message[ERROR_ILLEGALRESTARTADDRESS], "%s", expression);
                value |= f->opcode;
                width = 1;
                break;
            case FIX_IM:
                if(value < 0 || value > 2) { FIX_ERROR(message[ERROR_ILLEGALINTERRUPTMODE], "%s", expression); break; }
                value = f->opcode | ((value ? value + 1 : 0) << 3);
                width = 1;
                break;
        }
#undef FIX_ERROR
#undef FIX_WARNING
        if(errorcount) break;
        // Disk failures report immediately inside I/O; establish their context
        // only for spilled output. Resident patches cannot need file operations.
        if(f->simple && ioOutputPosition() > OUTPUT_BUFFERSIZE) restoreContext(f);
        offset = f->offset;
        count = f->count;
        while(count-- && !errorcount) {
            ioPatchValue(offset, &value, width);
            offset += width;
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
