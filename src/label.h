#ifndef LABEL_H
#define LABEL_H

#include "defines.h"

label_t *findLabel(const char *name);
void initGlobalLabelTable(void);
void initAnonymousLabelTable(void);
void writeAnonymousLabel(uint24_t address);
void readAnonymousLabel(void);
label_t * findGlobalLabel(const char *name);
uint16_t getGlobalLabelCount(void);
void saveGlobalLabelTable(void);
void advanceAnonymousLabel(void);
void definelabel(uint24_t num);

// definelabel() has nothing to do when the line carries no label, in either
// pass: pass 1 returns straight away, and pass 2 only ever sets the label
// scope. Most lines carry no label, and emit_instruction() calls this for
// every instruction it emits, so ask before making the call.
#define DEFINELABEL(num)    do { if(currentline.label) definelabel(num); } while(0)

extern uint24_t labelmemsize;

#endif // LABEL_H
