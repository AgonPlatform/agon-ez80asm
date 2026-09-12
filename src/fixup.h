#ifndef FIXUP_H
#define FIXUP_H
#include "defines.h"

/* Widths 1..4 are little-endian data; the remaining kinds patch opcode fields. */
enum { FIX_DS = 0, FIX_REL = 5, FIX_DISP, FIX_BIT, FIX_RST, FIX_IM };
typedef struct fixup fixup_t;
extern fixup_t *lastFixup;
extern bool expressionUnknown, resolvingFixups;
extern uint24_t fixupmemsize, fixupmempeak;
fixup_t *captureFixup(const char *expression);
fixup_t *captureSymbolFixup(label_t *symbol, const char *name);
void attachFixupAt(fixup_t *f, uint8_t kind, uint24_t count, uint8_t opcode, uint24_t offset);
void negateFixup(fixup_t *f);
void attachFixup(fixup_t *f, uint8_t kind, uint24_t count, uint8_t opcode);
void resolveFixups(void);
void freeFixups(void);
#endif
