#ifndef LISTING_H
#define LISTING_H

#include "defines.h"

void listInit(void);
void listFinish(void);
void listStartLine(const char *line, unsigned int linenumber);
void listEndLine(void);
void listPrintDSLines(int number, int value);
void listEmit8bit(uint8_t value);
void listPrintComment(const char *src);

#endif // LISTING_H
