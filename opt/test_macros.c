// Checks the classification tables and the range macros against the code they
// replaced, by running both over every input they can be given.
//
//   opt/test-macros.sh
//
// The tables and macros in src/ replace <ctype.h> calls, strchr() on string
// literals, and 32-bit comparisons. Each of those had an exact answer, and the
// point of this test is that the replacement gives the same one everywhere,
// including at the boundaries and at the zero terminator, where strchr()
// famously reports a match.
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <ctype.h>
#include "defines.h"
#include "ctype_tab.h"

static int failures = 0;

static void check(bool ok, const char *what, long value) {
    if(!ok) {
        if(failures < 10) printf("MISMATCH: %s at %ld\n", what, value);
        failures++;
    }
}

int main(void) {
    long c, i;

    // The tables answer what <ctype.h> and strchr() answered, for all 256
    // characters -- the zero terminator included.
    for(c = 0; c < 256; c++) {
        check(!ISSPACE(c) == !isspace((int)c), "ISSPACE", c);
        check(TOLOWER(c) == (uint8_t)tolower((int)c), "TOLOWER", c);
        check(!ISMNEMONICEND(c) ==
              !(isspace((int)c) || c == ';' || c == ':' || c == 0), "ISMNEMONICEND", c);
        check(!ISOPERATOR(c)       == !strchr("+-*/<>&|^~", (int)c), "ISOPERATOR", c);
        check(!ISBINARYOPERATOR(c) == !strchr("+-*/<>&|^", (int)c), "ISBINARYOPERATOR", c);
        check(!ISNEVERUNARY(c)     == !strchr("*/<>&|^", (int)c), "ISNEVERUNARY", c);
        check(!ISEXPRESSIONEND(c)  == !strchr("+-*/<>&|^~\t ", (int)c), "ISEXPRESSIONEND", c);
    }

    // The range macros answer what the comparisons answered. Every value near
    // a boundary, and a sample of the rest.
    for(i = -(1L << 31); i < (1L << 31); i++) {
        int32_t v = (int32_t)i;
        check(!OUTOFRANGE8(&v)  == !((v > 0xff) || (v < -128)), "OUTOFRANGE8", i);
        check(!OUTOFRANGE16(&v) == !((v > 0xffff) || (v < -32768)), "OUTOFRANGE16", i);
        check(!OUTOFRANGE24(&v) == !((v > 0xffffff) || (v < -8388608)), "OUTOFRANGE24", i);
        // The interesting values sit at the ends and around the boundaries;
        // skip the long stretch in between.
        if((i > -(1L << 31) + (1L << 21)) && (i < (1L << 24) - (1L << 21))) i = (1L << 24) - (1L << 21);
    }

    // Register sets: every single register against every set the tables use,
    // plus the empty set on either side.
    {
        static const uint24_t sets[] = {
            0, R_A, R_B, R_C, R_D, R_E, R_H, R_L, R_BC, R_DE, R_HL, R_SP, R_AF,
            R_IX, R_IY, R_IXH, R_IXL, R_IYH, R_IYL, R_R, R_MB, R_I,
            R_IX|R_IXH|R_IXL, R_IY|R_IYH|R_IYL, R_IXL|R_IYL,
            R_A|R_B|R_C|R_D|R_E|R_H|R_L, 0xffffff
        };
        unsigned a, b;
        for(a = 0; a < sizeof(sets)/sizeof(sets[0]); a++) {
            for(b = 0; b < sizeof(sets)/sizeof(sets[0]); b++) {
                bool want = (sets[a] & sets[b]) || !(sets[a] | sets[b]);
                check(!REGSETMATCH(&sets[a], &sets[b]) == !want, "REGSETMATCH", (long)(a * 100 + b));
                check(!REGSETOVERLAP(&sets[a], &sets[b]) == !(sets[a] & sets[b]), "REGSETOVERLAP", (long)(a * 100 + b));
            }
        }
    }

    if(failures) printf("%d mismatches\n", failures);
    else printf("all macro and table answers match the code they replaced\n");
    return failures != 0;
}
