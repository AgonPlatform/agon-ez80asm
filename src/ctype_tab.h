#ifndef CTYPE_TAB_H
#define CTYPE_TAB_H

#include <stdint.h>

/*
 * Table-driven replacements for the two <ctype.h> functions the assembler
 * calls per character: isspace() and tolower().
 *
 * On the eZ80 those are library calls, so a line of source costs one call per
 * character just to find where the mnemonic starts. A 256-byte table turns
 * each of them into an index and a load, and the macro form leaves no function
 * for the compiler to decide about.
 *
 * The tables hold exactly what the "C" locale defines, so the answers are the
 * same ones <ctype.h> gave: space is 0x09-0x0D and 0x20, and only 'A'-'Z' fold
 * to lowercase. Characters at 0x80 and above are not space and do not fold,
 * which is what isspace()/tolower() already returned for them.
 */
extern const uint8_t ctype_space[256];
extern const uint8_t ctype_lower[256];

#define ISSPACE(c) (ctype_space[(uint8_t)(c)])
#define TOLOWER(c) (ctype_lower[(uint8_t)(c)])

#endif // CTYPE_TAB_H
