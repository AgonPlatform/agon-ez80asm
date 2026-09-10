#ifndef GLOBALS_H
#define GLOBALS_H

#include <stdbool.h>
#include <stdint.h>
#include "config.h"
#include "defines.h"

// Global variables
extern bool relocate;
extern uint24_t relocateBaseAddress;
extern uint24_t relocateOutputBaseAddress;
extern uint8_t cputype;
extern uint8_t contentlevel;
extern uint8_t errorreportlevel;
extern uint8_t maxstackdepth;
extern contentitem_t *currentcontentitem;
extern uint16_t sourcefilecount;
extern uint16_t binfilecount;
extern uint24_t filecontentsize;
extern unsigned int macrolinenumber;
extern unsigned int pass;
extern conditionalstate_t inConditionalSection;
extern macro_t *currentExpandedMacro;
extern uint8_t macrolevel;
extern uint16_t macroexpansions;
extern uint24_t address;
extern uint16_t errorcount;
extern bool adlmode;
extern tokenline_t currentline;
extern bool listing;                // list_enabled || consolelist_enabled
extern bool list_enabled;
extern bool consolelist_enabled;
extern uint8_t fillbyte;
extern uint24_t start_address;
extern bool coloroutput;
extern unsigned int labelcollisions;
extern bool ignore_truncation_warnings;
extern bool issue_warning;
extern uint24_t remaining_dsspaces;
extern bool exportsymbols, displaystatistics;

// Global parsed results
extern uint8_t suffix;      // per-instruction suffix code
extern operand_t operand1;
extern operand_t operand2;
extern opcodesequence_t output;

// CPU name strings
extern char *cpuname[];

// Error messages
extern char *message[];

#endif // GLOBALS_H
