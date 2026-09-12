#ifndef STR2NUM_H
#define STR2NUM_H

#include "config.h"
#include "defines.h"

// Transforms a binary/hexadecimal/decimal string to an int24_t number
// option to NOT halt on errors, but just set the err_str2num status - just check if something is a valid number
int32_t str2num(const char *string, uint8_t length); // string token length
/* Invalid conversion results are unspecified; inspect err_str2num first. */
int32_t str2numOrLabel(const char *string, uint8_t length);
int32_t str2hex(const char *string);
bool    isvalidNumber(const char *string);
extern bool err_str2num;

#endif // STR2NUM_H