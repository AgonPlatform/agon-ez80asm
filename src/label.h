#ifndef LABEL_H
#define LABEL_H

#include "defines.h"

label_t *findLabel(const char *name);
/* Only after findLabel returned NULL, with no intervening table mutation. */
label_t *createUnresolvedLabel(const char *name);
void initGlobalLabelTable(void);
void initAnonymousLabelTable(void);
void writeAnonymousLabel(uint24_t address);
void *anonymousPosition(void);
void restoreAnonymousPosition(void *position);
label_t * findGlobalLabel(const char *name);
uint24_t getGlobalLabelCount(void);
void saveGlobalLabelTable(void);
void advanceAnonymousLabel(void);
void definelabel(uint24_t num);

// Most lines carry no label. Avoid a call for every emitted instruction.
#define DEFINELABEL(num)    do { if(currentline.label) definelabel(num); } while(0)

extern uint24_t labelmemsize;

#endif // LABEL_H
