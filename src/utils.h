#ifndef UTILS_H
#define UTILS_H

#include "config.h"
#include "defines.h"

/* Token parsers
 ***************
 *   Name                  - Ends on    - Literal transparent - String transparent
 *   -----------------------------------------------------------------------------
 *   getLabelToken()       - :/;/0      - No                  - No
 *   getMnemonicToken()    - ;/spaces/0 - No                  - No
 *   getOperandToken()     - ;/,/0      - Yes                 - No
 *   getDefineValueToken() - ;/,/=/0    - Yes                 - Yes
 * 
 */
uint8_t  getMnemonicToken(streamtoken_t *token, char *src);
uint8_t  getOperandToken(streamtoken_t *token, char *src);
uint8_t  getDefineValueToken(streamtoken_t *token, char *src);

uint16_t getnextContentLine(char *dst1, contentitem_t *ci);
uint16_t getlastContentLine(char *dst1, contentitem_t *ci);
uint16_t getnextMacroLine(char **ptr, char *dst);
char *   allocateString(const char *name, uint24_t *bytecounter);
void *   allocateMemory(size_t size, uint24_t *bytecounter);
uint8_t  strcompound(char *dest, const char *src1, const char *src2);
void     validateRange8bit(const int32_t *value, const char *name);
void     validateRange16bit(const int32_t *value, const char *name);
void     validateRange24bit(const int32_t *value, const char *name);
void     remove_ext (char* myStr, char extSep, char pathSep);
void     trimRight(char *str);
void     error(const char *msg, const char *contextformat, ...);
void     warning(const char *msg, const char *contextformat, ...);
void     colorPrintf(int color, const char *msg, ...);
int32_t  getExpressionValue(char *str, requiredResult_t requiredPass);
uint8_t  getEscapedChar(char c);
uint8_t  getLiteralValue(const char *string);
void     errorCPUtype(errormessage_t index);

#endif // UTILS_H

