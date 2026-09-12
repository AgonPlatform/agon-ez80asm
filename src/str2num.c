#include <stdint.h>
#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include "ctype_tab.h"
#include "globals.h"
#include "config.h"
#include "defines.h"

bool err_str2num;

// transform a binary string to a uint32_t number
// string must end with 0 and contain only valid characters (0..1)
int32_t str2bin(const char *string) {
    uint32_t result = 0;
    uint8_t x = 0;

    while(*string) {
        if((*string == '0') || (*string == '1')) {x = *string - '0';}
        else err_str2num = true;
        result = (result << 1) | x;
        string++;
    }
    return result;
}

// transform a hex string to a int32_t number
// string must end with 0 and contain only valid characters (0..9,a..f,A..F)
int32_t str2hex(const char *string) {
    uint32_t result = 0;
    char c;
    uint8_t x = 0;

    while(*string) {
        c = *string;
        if((c >= '0') && (c <= '9')) { x = c - '0'; }
        else {
            c = c & 0xDF ; // toupper();
            if((c >= 'A') && (c <= 'F')) { x = c - 'A' + 10; }
        else err_str2num = true;
        }
        result = (result << 4) | x;
        string++;
    }
    return result;
}

// transform a hex string to a int32_t number
// string must end with 0 and contain only valid characters (0..9)
int32_t str2dec(const char *string) {
    uint32_t result = 0;
    uint8_t x = 0;

    while(*string) {
        if((*string >= '0') && (*string <= '9')) { x = *string - '0'; }
        else err_str2num = true;
        result = ((result << 1) + (result << 3)) + x;
        string++;
    }
    return result;
}

/* Suffix forms need a bounded read, not a temporary NUL-terminated copy.
 * Keep the legacy invalid-digit behavior (reuse the previous digit), since
 * some callers inspect the result independently of err_str2num. */
static int32_t suffixedNumber(const char *string, uint8_t length, bool hex) {
    uint32_t result = 0;
    uint8_t x = 0;
    while(length-- && *string) {
        uint8_t c = (uint8_t)*string++;
        if(hex) {
            if(c >= '0' && c <= '9') x = c - '0';
            else {
                c &= 0xDF;
                if(c >= 'A' && c <= 'F') x = c - 'A' + 10;
                else err_str2num = true;
            }
            result = (result << 4) | x;
        }
        else {
            if(c == '0' || c == '1') x = c - '0';
            else err_str2num = true;
            result = (result << 1) | x;
        }
    }
    return result;
}

// Transforms a binary/hexadecimal/decimal string to an uint32_t number
// Valid strings are
// BINARY:  %..., , 0b..., ...b, capital letters allowed
// HEX:     0x..., ...h, $..., capital letters allowed
// DECIMAL ...
// Returns current program counter with just '$'
int32_t str2num(const char *string, uint8_t length) {
    char lastchar;
    int32_t result = 0;
    err_str2num = false;

    if(*string == '$') {
        if(*(string+1) == 0) {
            if(relocate) return (relocateBaseAddress + (address - relocateOutputBaseAddress));
            else return address;
        }
        result = str2hex(string+1);
        return result;
    }
    if(*string == '#') {
        result = str2hex(string+1);
        return result;
    }
    if(*string == '%') {
        result = str2bin(string+1);
        return result;
    }

    if(length == 1) {
        uint8_t digit = (uint8_t)(*string - '0');
        if(digit <= 9 && !string[1]) return digit;
        return str2dec(string);
    }

    lastchar = TOLOWER(string[length-1]);
    
    if(lastchar == 'h') {
        return suffixedNumber(string, length-1, true);
    }

    if((*string == '0') && (length >= 2)) {
        if(TOLOWER(*(string+1)) == 'x') {
            result = str2hex(string+2);
            return result;
        }
        if(TOLOWER(*(string+1)) == 'b') {
            result = str2bin(string+2);
            return result;
        }
    }

    if(lastchar == 'b') {
        return suffixedNumber(string, length-1, false);
    }

    if(length == 2 && !string[2]) {
        uint8_t tens = (uint8_t)(string[0] - '0');
        uint8_t ones = (uint8_t)(string[1] - '0');
        if(tens <= 9 && ones <= 9) return (uint8_t)(tens * 10 + ones);
    }
    result = str2dec(string);
    return result;
}

bool isvalidNumber(const char *string) {
    str2num(string, strlen(string));
    return !err_str2num;
}


/* Symbol-related callers only consume a result when err_str2num is false.
 * Reject impossible numeric spellings before entering the radix loops. Keep
 * numeric-looking names on the original parser, including hex/binary suffixes.
 * The original str2num entry remains unchanged for callers that inspect its
 * return value even when conversion fails. */
int32_t str2numOrLabel(const char *string, uint8_t length) {
    if(length) {
        uint8_t first = (uint8_t)string[0];
        uint8_t last = TOLOWER(string[length-1]);
        if((uint8_t)(first - '0') > 9 && first != '$' && first != '#' &&
           first != '%' && last != 'h' && last != 'b') {
            err_str2num = true;
            return 0;
        }
    }
    return str2num(string, length);
}
