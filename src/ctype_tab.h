#ifndef CTYPE_TAB_H
#define CTYPE_TAB_H

#include <stdint.h>

/*
 * Character classification by table lookup.
 *
 * The tokeniser and the expression parser ask the same handful of questions
 * about every character of every line, twice over, once per pass. Written out
 * in C those questions are either library calls -- isspace(), tolower(),
 * strchr() -- or chains of comparisons, and on the eZ80 both are expensive
 * next to an index and a load. A 256-byte table per question answers it in one
 * step, and the macro form leaves no function for the compiler to decide
 * whether to inline.
 *
 * Each table answers exactly what the code it replaced answered, including for
 * the zero terminator. That matters more than it looks: strchr(set, 0) finds
 * the set's own terminator and returns non-NULL, so the parser was relying on
 * "is this an operator?" being true at end of string. The operator table says
 * so too.
 */

// Values in ctype_operator[], ordered so that each set of operators contains
// the ones above it: ISOPERATOR() is any of the three, ISBINARYOPERATOR()
// the top two, ISNEVERUNARY() only the last.
#define OPERATOR_UNARYONLY      1   // ~
#define OPERATOR_BINARY         2   // + and -, which may also be unary
#define OPERATOR_NEVERUNARY     3   // * / < > & | ^

extern const uint8_t ctype_space[256];
extern const uint8_t ctype_lower[256];
extern const uint8_t ctype_mnemonicend[256];
extern const uint8_t ctype_operator[256];
extern const uint8_t ctype_exprend[256];

#define ISSPACE(c)          (ctype_space[(uint8_t)(c)])
#define TOLOWER(c)          (ctype_lower[(uint8_t)(c)])

// Ends a mnemonic: whitespace, a comment, a label colon, or end of line.
#define ISMNEMONICEND(c)    (ctype_mnemonicend[(uint8_t)(c)])

// An expression operator, and the two narrower sets the parser tests for.
#define ISOPERATOR(c)       (ctype_operator[(uint8_t)(c)] != 0)
#define ISBINARYOPERATOR(c) (ctype_operator[(uint8_t)(c)] >= OPERATOR_BINARY)
#define ISNEVERUNARY(c)     (ctype_operator[(uint8_t)(c)] >= OPERATOR_NEVERUNARY)

// Ends the name or number the expression parser is reading: an operator,
// whitespace, or end of string.
#define ISEXPRESSIONEND(c)  (ctype_exprend[(uint8_t)(c)])

#endif // CTYPE_TAB_H
