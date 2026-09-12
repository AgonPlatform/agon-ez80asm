#include <stddef.h>
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

/* Fixups share a lifetime. Keep their addresses stable in chunks and release
 * the chunks together, rather than inserting every record into libc's free
 * list. The union and rounded sizes also align records on host builds. */
#ifndef FIXUP_BLOCK_SIZE
#define FIXUP_BLOCK_SIZE 2048
#endif
typedef struct fixup_block {
    struct fixup_block *next;
    union { fixup_t alignment; char bytes[1]; } data;
} fixup_block_t;
struct fixup_alignment { char byte; fixup_t record; };
static fixup_block_t *blocks;
static char *blockcursor;
static size_t blockremaining;

static fixup_t *allocateFixup(size_t size) {
    const size_t alignment = offsetof(struct fixup_alignment, record);
    const size_t header = offsetof(fixup_block_t, data);
    fixup_t *result;
    size = ((size + alignment - 1) / alignment) * alignment;
    if(size > blockremaining) {
        size_t capacity = size > FIXUP_BLOCK_SIZE ? size : FIXUP_BLOCK_SIZE;
        fixup_block_t *block = NULL;
        /* A mostly empty chunk must not cause an otherwise avoidable OOM.
         * Try the preferred capacity silently, then use the normal error
         * handling for an allocation just large enough for this record. */
        if(capacity > size) block = malloc(header + capacity);
        if(block) fixupmemsize += header + capacity;
        else {
            capacity = size;
            block = allocateMemory(header + capacity, &fixupmemsize);
            if(!block) return NULL;
        }
        block->next = blocks;
        blocks = block;
        blockcursor = block->data.bytes;
        blockremaining = capacity;
    }
    result = (fixup_t *)blockcursor;
    blockcursor += size;
    blockremaining -= size;
    return result;
}

static fixup_t *capture(const char *expression, label_t *symbol) {
    size_t size = sizeof(fixup_t) + (symbol ? 0 : strlen(currentcontentitem->labelscope)) + strlen(expression) + 1;
    fixup_t *f = allocateFixup(size);
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

// Keep the value behind a pointer: inlining these checks lets the eZ80
// compiler replace byte loads with expensive 32-bit shift helpers.
#if defined(__GNUC__)
__attribute__((noinline))
#endif
static bool fixupOutOfRange(const int32_t *value, uint8_t width) {
    switch(width) {
        case 1: return OUTOFRANGE8(value);
        case 2: return OUTOFRANGE16(value);
        case 3: return OUTOFRANGE24(value);
        default: return false;
    }
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
        if(f->negative) value = -(int24_t)value;
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
                if(!ignore_truncation_warnings && fixupOutOfRange(&value, 1)) {
                    if(f->simple) restoreContext(f);
                    validateRange8bit(&value, expression);
                }
                break;
            case 2:
                if(!ignore_truncation_warnings && fixupOutOfRange(&value, 2)) {
                    if(f->simple) restoreContext(f);
                    validateRange16bit(&value, expression);
                }
                break;
            case 3:
                if(!ignore_truncation_warnings && fixupOutOfRange(&value, 3)) {
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
                value = (int24_t)value;
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
    while(blocks) {
        fixup_block_t *next = blocks->next;
        free(blocks);
        blocks = next;
    }
    blockcursor = NULL;
    blockremaining = 0;
    first = last = lastFixup = NULL;
    fixupmemsize = 0;
}
