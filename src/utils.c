#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdint.h>
#include <stdarg.h>
#include "ctype_tab.h"
#include "defines.h"
#include "globals.h"
#include "console.h"
#include "utils.h"
#include "str2num.h"
#include "label.h"
#include "instruction.h"
#include "io.h"
#include "assemble.h"

// memory allocate size bytes, raise error if not available
void *allocateMemory(size_t size, uint24_t *bytecounter) {
    void *ptr = malloc(size);
    if(ptr == NULL) {
        error(message[ERROR_MEMORY],0);
    }
    *bytecounter += size;
    return ptr;
}

// memory allocate a string, copy content and return pointer, or NULL if no memory
char *allocateString(const char *name, uint24_t *bytecounter) {
    char *ptr = (char *)allocateMemory(strlen(name) + 1, bytecounter);
    if(ptr) {
        strcpy(ptr, name);
    }
    return ptr;
}

// return a base filename, stripping the given extension from it
void remove_ext (char* myStr, char extSep, char pathSep) {
    char *lastExt, *lastPath;
    // Error checks.
    if (myStr == NULL) return;
    // Find the relevant characters.
    lastExt = strrchr (myStr, extSep);
    lastPath = (pathSep == 0) ? NULL : strrchr (myStr, pathSep);
    // If it has an extension separator.
    if (lastExt != NULL) {
        // and it's to the right of the path separator.
        if (lastPath != NULL) {
            if (lastPath < lastExt) {
                // then remove it.
                *lastExt = '\0';
            }
        } else {
            // Has extension separator with no path separator.
            *lastExt = '\0';
        }
    }
}

void displayerror(const char *msg, const char *context, errorlevel_t level) {
    contentitem_t *ci = currentcontentitem;

    if(level == LEVEL_WARNING) {
        vdp_set_text_colour(YELLOW);
    }
    else {
        vdp_set_text_colour(RED);
    }

    if((errorcount == 1) || (level == LEVEL_WARNING)) {
        if(ci) {
            if(currentExpandedMacro) {
                printf("Macro [%s] in \"%s\" line %d - ",currentExpandedMacro->name, currentExpandedMacro->originfilename, currentExpandedMacro->originlinenumber+macrolinenumber);
            }
            else {
                printf("File \"%s\" line %d - ", ci->name, ci->currentlinenumber);
            }
        }
        printf("%s", msg);
        if(strlen(context)) {
            if(level == LEVEL_WARNING)
                vdp_set_text_colour(BRIGHT_WHITE);
            else
                vdp_set_text_colour(YELLOW);
            printf(" \'%s\'", context);
        }
        printf("\n");
    }
    vdp_set_text_colour(BRIGHT_WHITE);
}

void error(const char *msg, const char *contextformat, ...) {
    char context[LINEMAX+1];

    if(contextformat) {
        va_list args;
        va_start(args, contextformat);
        vsprintf(context, contextformat, args);
        va_end(args);
    }
    else context[0] = 0;

    errorcount++;
    errorreportlevel = contentlevel;

    displayerror(msg, context, LEVEL_ERROR);
}

void colorPrintf(int color, const char *msg, ...) {
    vdp_set_text_colour(color);
    va_list args;
    va_start(args, msg);
    vprintf(msg, args);
    vdp_set_text_colour(BRIGHT_WHITE);
}

void warning(const char *msg, const char *contextformat, ...) {
    char context[LINEMAX+1];

    if(contextformat) {
        va_list args;
        va_start(args, contextformat);
        vsprintf(context, contextformat, args);
        va_end(args);
    }
    else context[0] = 0;

    issue_warning = true;

    displayerror(msg, context, LEVEL_WARNING);
}

void trimRight(char *str) {
    while(*str) str++;
    str--;
    while(ISSPACE(*str)) str--;
    str++;
    *str = 0;
}

// fill the streamtoken_t object, according to the stream
// returns the number of Mnemonic characters found, or 0 if none
uint8_t getMnemonicToken(streamtoken_t *token, char *src) {
    uint8_t length = 0;

    // skip leading space
    while(ISSPACE(*src)) src++;
    if(*src == 0) {
        memset(token, 0, sizeof(streamtoken_t));
        return 0;
    }
    token->start = src;
    while(!ISMNEMONICEND(*src)) {
        length++;
        src++;
    }
    token->terminator = *src;
    if(*src) token->next = src+1;
    else token->next = src;

    *src = 0; // terminate stream
    return length;
}

// fill the streamtoken_t object, parse it as an operand
// returns the number of Operator characters found, or 0 if none
uint8_t getOperandToken(streamtoken_t *token, char *src) {
    uint8_t length = 0;
    bool inliteral = false;

    // skip leading space
    while(ISSPACE(*src)) src++;
    if(*src == 0) {
        memset(token, 0, sizeof(streamtoken_t));
        return 0;
    }
    token->start = src;

    // hunt for end-character (0 , or ; in normal non-literal mode)
    //
    // Only three characters can matter, so ordinary ones get past on three
    // comparisons with no state to load; the literal flag is only consulted
    // once one of the three turns up.
    while(*src) {
        if((*src == ',') || (*src == ';') || (*src == '\'')) {
            if(*src == '\'') {
                if(inliteral) {
                    if(*(src+1) == '\'') {
                        src++;
                        length++;
                    }
                    inliteral = false;
                }
                else inliteral = true;
            }
            else if(!inliteral) break;
        }
        src++;
        length++;
    }

    token->terminator = *src;
    if(*src) token->next = src+1;
    else token->next = src;

    if(length) {
        *src-- = 0; // terminate early and revert one character
        while(ISSPACE(*src)) { // remove trailing space(s)
            *src-- = 0; // terminate on trailing spaces
            if(length-- == 0) break;
        }
    }
    return length;
}

// Returns a bracketed token
// Example '[token]' -> returns 'token'0
// Assumes start on the '[' character
// Ends the token string in the stream on the matching ']' character, by zeroing it
uint8_t getBracketToken(streamtoken_t *token, char *src) {
    uint8_t length = 0;
    uint8_t opencount = 1;

    token->start = src+1;
    src++;
    while(*src) {
        if(*src == '[') opencount++;
        if(*src == ']') opencount--;
        if(opencount == 0) {
            token->next = src+1;
            *src = 0;
            break;
        }
        src++;
        length++;
    }
    if(opencount) return 0;
    else return length;
}

uint8_t getDefineValueToken(streamtoken_t *token, char *src) {
    uint8_t length = 0;
    tokenclass_t state;
    bool escaped = false;
    bool terminated;

    // skip leading space
    while(ISSPACE(*src)) src++;
    if(*src == 0) {
        memset(token, 0, sizeof(streamtoken_t));
        return 0;
    }
    token->start = src;

    switch(*src) {
        case '\"':
            state = TOKEN_STRING;
            src++;
            length++;
            break;
        case '\'':
            state = TOKEN_LITERAL;
            src++;
            length++;
            break;
        case '(':
            state = TOKEN_BRACKET;
            src++;
            length++;
            break;
        default:
            state = TOKEN_REGULAR;
    }

    while(*src) {
        terminated = false;
        switch(state) {
            case TOKEN_STRING:
                switch(*src) {
                    case '\\':
                        escaped = !escaped;
                        break;
                    case '\"':
                        if(!escaped) state = TOKEN_REGULAR;
                        escaped = false;
                        break;
                    default:
                        escaped = false;
                        break;
                }
                break;
            case TOKEN_LITERAL:
                if(*src == '\'') {
                    if(*(src+1) == '\'') {
			            src++;
                    	length++;
		            }
        		    state = TOKEN_REGULAR;
                }
                break;
            case TOKEN_BRACKET:
                if(*src == ')') state = TOKEN_REGULAR;
                break;
            case TOKEN_REGULAR:
                terminated = ((*src == ';') || (*src == ',') || (*src == '='));
                break;            
        }
        if(terminated) break;
        src++;
        length++;
    }

    token->terminator = *src;
    if(*src) token->next = src+1;
    else token->next = src;

    if(length) {
        *src-- = 0; // terminate early and revert one character
        while(ISSPACE(*src)) { // remove trailing space(s)
            *src-- = 0; // terminate on trailing spaces
            if(length-- == 0) break;
        }
    }
    if(state == TOKEN_STRING) error(message[ERROR_STRING_NOTTERMINATED],0);
    return length;
}

// These take the value by address, not by value. The range macros read the
// bytes of the word, and given a value it is holding in registers the compiler
// will happily produce those bytes with 32-bit shifts -- library calls, the
// very thing the macro exists to avoid. Given an address it has to load them.
void validateRange8bit(const int32_t *value, const char *name) {
    if(!(ignore_truncation_warnings)) {
        if(OUTOFRANGE8(value)) {
            warning(message[WARNING_TRUNCATED_8BIT],"%s",name);
        }
    }
}

void validateRange16bit(const int32_t *value, const char *name) {
    if(!(ignore_truncation_warnings)) {
        if(OUTOFRANGE16(value)) {
            warning(message[WARNING_TRUNCATED_16BIT],"%s",name);
        }
    }
}

void validateRange24bit(const int32_t *value, const char *name) {
    if(!(ignore_truncation_warnings)) {
        if(OUTOFRANGE24(value)) {
            warning(message[WARNING_TRUNCATED_24BIT],"%s",name);
        }
    }
}

// Returns the value of an escaped character \c, or 255 if illegal
uint8_t getEscapedChar(char c) {
    switch(c) {
        case 'a':
            return(0x07); // Alert, beep
        case 'b':
            return(0x08); // Backspace
        case 'e':
            return(0x1b); // Escape
        case 'f':
            return(0x0c); // Formfeed
        case 'n':
            return(0x0a); // Newline
        case 'r':
            return(0x0d); // Carriage return
        case 't':
            return(0x09); // Horizontab tab
        case 'v':
            return(0x0b); // Vertical tab
        case '\\':
            return('\\'); // Backslash
        case '\'':
            return('\''); // Single quotation mark
        case '\"':
            return('\"'); // Double quotation mark
        case '?':
            return('?');  // Question mark
        default:
            return(0xff);
    }
}

// Get the ascii value from a single 'x' token.
uint8_t getLiteralValue(const char *string) {

    uint8_t len = strlen(string);
    if((len == 3) && (string[2] == '\'')) {
        return string[1];
    }

    if((len == 4) && (string[1] == '\\') && (string[3] == '\'')) {
        uint8_t c = getEscapedChar(string[2]);
        if(c == 0xff) {
            error(message[ERROR_ILLEGAL_ESCAPELITERAL],0);
            return 0;
        }
        return c;
    }

    error(message[ERROR_ASCIIFORMAT],0);
    return 0;
}

// Resolves a number from a string in this order:
// 1) Check if a label exists with this name
// 2) If not, try converting it to a number with str2num
int32_t resolveNumber(char *str, uint8_t length, requiredResult_t requiredPass) {
    int32_t number;
    label_t *lbl = findLabel(str);

    if((pass == STARTPASS) && (requiredPass == REQUIRED_LASTPASS)) return 0;

    if(lbl) number = lbl->address;
    else {
        if(*str == '\'') number = getLiteralValue(str);
        else {
            number = str2num(str, length?length:strlen(str));
            if(err_str2num) {
                error(message[ERROR_IDENTIFIER], "%s", str);                            
                return 0;
            }
        }
    }
    return number;
}

// copy a literal token from a string to a destination string
// with up to 4 characters, according to the literal specification
// 'x'  - value of character x
// '\'  - value of '\' character, this is a special
// '\x' - value of escaped x character
// '\'' - value of ' character, this is a special
//
// no spaces are allowed in the literal
//
// partial literal characters in the correct position are copied, with a shorter
// destination string length
uint8_t copyLiteralToken(const char *from, char *to) {
    uint8_t len, n;

    if(from == 0) len = 0;
    else {
        if(from[1] == 0) len = 1;
        else {
            if(from[1] == '\\') { // escaped
                if(from[2] == 0) len = 2; // error - just two characters
                else {
                    if(from[2] == '\'') {
                        if(from[3] == '\'') len = 4; // special 4 '\''
                        else len = 3;
                        // combining two cases
                        // if(from[3] == 0) len = 3; // error
                        // else len = 3; // '\'
                    } 
                    else {
                        if(from[3] == 0) len = 3;
                        else len = 4; // '\x'
                    }
                }
            }
            else {
                if(from[2] == 0) len = 2; // error - just two characters
                else len = 3; // regular 'x'
            }
        }
    }
    n = len;
    while(n--) *to++ = *from++;
    *to = 0;
    return len;
}

// Gets the value from an expression, possible consisting of values, labels and operators
int32_t getExpressionValue(char *str, requiredResult_t requiredPass) {
    uint8_t tmplength;
    streamtoken_t token;
    char buffer[LINEMAX+1];
    char *bufptr, *errptr;
    char operator, unaryoperator;
    int32_t tmp = 0;
    int32_t total = 0;
    getValueState_t state;;

    if((pass == STARTPASS) && (requiredPass == REQUIRED_LASTPASS)) return 0;

    while(ISSPACE(*str)) str++; // eat all spaces
    errptr = str;

    operator = 0; // first implicit operator
    unaryoperator = 0;
    state = START;

/*  State machine
 *
 *  [START] -num-> [NUMBER] - string ends -> [return total]
 *   ^    |          ^   |
 *   |  op\          |   |
 *   |     ---> [UNARY]  |
 *   \                   /
 *    --------<- [OP] <--
 */
    while(true) {
        switch(state) {
            case UNARY:
                unaryoperator = *str++;
                while(ISSPACE(*str)) str++; // eat all spaces
                if(*str == 0) {
                    error(message[ERROR_MISSINGLABELORNUMBER],"%s",errptr);
                    return 0;
                }
                if(ISOPERATOR(*str)) {
                    error(message[ERROR_UNARYOPERATOR],0);
                    return 0;
                }
                state = NUMBER;
                break;
            case OP:
                if((*str == '<') || (*str == '>')) {                
                    if(*(str+1) != *str) {
                        error(message[ERROR_OPERATOR], 0);
                        return 0;
                    }
                    str++; // advance the double operator
                }
                unaryoperator = 0;
                operator = *str++;
                if(!ISBINARYOPERATOR(operator)) { // illegal operator
                    error(message[ERROR_OPERATOR], "%c", operator);
                    return 0;
                }
                while(ISSPACE(*str)) str++; // eat all spaces
                if(*str == 0) {
                    error(message[ERROR_MISSINGLABELORNUMBER],"%s",errptr);
                    return 0;
                }
                if(ISNEVERUNARY(*str)) { // illegal unary
                    error(message[ERROR_UNARYOPERATOR], 0);
                    return 0;
                }
                state = START;
                // implicit fall-through for performance
            case START:
                if(ISOPERATOR(*str)) {
                    if((*str == '-') || (*str == '~') || (*str == '+')) {
                        state = UNARY;
                        break;
                    }
                    else {
                        error(message[ERROR_UNARYOPERATOR],0);
                        return 0;
                    }
                }
                else state = NUMBER;
                // implicit fall-through for performance
            case NUMBER:
                bufptr = buffer;
                switch(*str) {
                    case '\'':
                        tmplength = copyLiteralToken(str, buffer);
                        str += tmplength;
                        tmp = resolveNumber(buffer, tmplength, requiredPass);
                        break;
                    case '[':
                        if(getBracketToken(&token, str) == 0) {
                            error(message[ERROR_BRACKETFORMAT],0);
                            return 0;
                        }
                        tmp = getExpressionValue(token.start, requiredPass);
                        str = token.next;
                        break;
                    default:
                        while(!ISEXPRESSIONEND(*str)) *bufptr++ = *str++;
                        *bufptr = 0; // terminate string in buffer
                        tmp = resolveNumber(buffer, bufptr - buffer, requiredPass);
                        break;
                }
                
                if(unaryoperator) {
                    if(unaryoperator == '-') tmp = -tmp;
                    if(unaryoperator == '~') tmp = ~tmp;
                }

                switch(operator) {
                    case 0:
                    case '+': total += tmp; break;
                    case '-': total -= tmp; break;
                    case '*': total *= tmp; break;
                    case '<': total = total << tmp; break;
                    case '>': total = total >> tmp; break;
                    case '&': total = total & tmp;  break;
                    case '|': total = total | tmp;  break;
                    case '^': total = total ^ tmp;  break;
                    case '~': total = total + ~tmp; break;
                    case '/': total = total / tmp;  break;
                    default:
                        error(message[ERROR_OPERATOR],"%c",operator);
                        return 0;
                }
                // operation complete, reset operators 
                operator = 0;
                unaryoperator = 0;

                while(ISSPACE(*str)) str++; // eat all spaces
                if(*str) state = OP;
                else return total;
                break;
        }
    }
    return total;
}

// efficient strcpy/strcat compound function
uint8_t strcompound(char *dest, const char *src1, const char *src2) {
    uint8_t len = 0;

    while(*src1) {
        *dest++ = *src1++;
        len++;
    }
    while(*src2) {
        *dest++ = *src2++;
        len++;
    }
    *dest = 0;
    return len;
}

// Get the next line from a buffer, pointed to by *ptr
// The stringpointer *ptr will update after each line
uint16_t getnextMacroLine(char **ptr, char *dst) {
    char *_nextline_ptr = *ptr;
    uint16_t len = 0;

    while(*_nextline_ptr) {
        *dst++ = *_nextline_ptr;
        len++;
        if(*_nextline_ptr++ == '\n') {
            break;
        }
    }
    *dst = 0;
    *ptr = _nextline_ptr;
    return len;
}

// How many characters of a line may still be copied in one block.
//
// A line is at most LINEMAX characters, and the character at LINEMAX is only
// allowed when it is the newline that ends the line -- so a run that ends on a
// newline may be one longer than one that does not. Anything longer has to be
// copied a character at a time, because that is the path that reports where it
// stopped, and this is the rare case anyway.
#define LINERUNFITS(len, run, endsonnewline) \
    (((len) + (run)) <= (uint24_t)LINEMAX + ((endsonnewline) ? 1 : 0))

uint16_t _readFullBufferedLine(char *dst1, contentitem_t *ci) {
    uint16_t len = 0;
    char *ptr = ci->readptr;
    uint24_t remaining = ci->size - ci->filepos;
    char *end;
    uint24_t run;

    // The line runs to the next newline, or to the end of the file. An
    // embedded zero ends it early, the way reading character by character did,
    // but only the characters up to the newline need looking at for one.
    end = memchr(ptr, '\n', remaining);
    run = end ? (uint24_t)(end - ptr) + 1 : remaining;
    end = memchr(ptr, 0, run);
    if(end) run = (uint24_t)(end - ptr);

    if(LINERUNFITS(len, run, run && (ptr[run-1] == '\n'))) {
        memcpy(dst1, ptr, run);
        dst1 += run;
        ptr += run;
        len = run;
    }
    else {
        while(*ptr) {
            if((len++ == LINEMAX) && (*ptr != '\n')) {
                error(message[ERROR_LINETOOLONG],0);
                return 0;
            }
            *dst1++ = *ptr;
            if(*ptr++ == '\n') {
                break;
            }
        }
    }
    ci->readptr = ptr;
    ci->filepos += len;
    ci->lastreadlength = len;
    *dst1 = 0;
    return len;
}

// Used with '-m' minimum buffered configuration
//
// Refills the buffer so that it ends on a line boundary: whatever the last
// read left after its final newline is carried to the front, the file is read
// into the rest, and the usable end is pulled back to the last newline in what
// is now there. Every line in the buffer is then whole, which is what lets the
// reader below take one with a single memchr() and a single memcpy() and no
// loop -- a line cannot span a refill, so there is no partial line to carry
// around in the reader itself.
//
// Returns false at end of file, when nothing is left to read.
bool _fillLineBuffer(contentitem_t *ci) {
    unsigned int carry = ci->rawinbuffer - (unsigned int)(ci->readptr - ci->buffer);
    unsigned int got;
    unsigned int n;

    if(carry) memmove(ci->buffer, ci->readptr, carry);
    got = fread(ci->buffer + carry, 1, INPUT_BUFFERSIZE - carry, ci->fh);
    ci->readptr = ci->buffer;
    ci->rawinbuffer = carry + got;

    if(ci->rawinbuffer == 0) {
        ci->bytesinbuffer = 0;
        return false;
    }

    // A read shorter than asked for is the end of the file. What is left is
    // the last line, which needs no newline to be a line, so there is nothing
    // to pull back to.
    if(got < INPUT_BUFFERSIZE - carry) {
        ci->bytesinbuffer = ci->rawinbuffer;
        return true;
    }

    for(n = ci->rawinbuffer; n > 0; n--) {
        if(ci->buffer[n - 1] == '\n') {
            ci->bytesinbuffer = n;
            return true;
        }
    }

    // A full buffer with no newline in it: one line is longer than the buffer,
    // which is longer than LINEMAX, so the reader is about to complain about
    // it. Hand the whole thing over and let it.
    ci->bytesinbuffer = ci->rawinbuffer;
    return true;
}

// Reads a LINE from the buffer and returns its length
uint16_t _readMinimumBufferedLine(char *dst, contentitem_t *ci) {
    uint16_t len = 0;
    char *ptr;
    char *end;
    uint24_t run;

    if((ci->bytesinbuffer == 0) && !_fillLineBuffer(ci)) {
        *dst = 0;
        ci->lastreadlength = 0;
        return 0;
    }

    ptr = ci->readptr;
    end = memchr(ptr, '\n', ci->bytesinbuffer);
    run = end ? (uint24_t)(end - ptr) + 1 : ci->bytesinbuffer;

    if(LINERUNFITS(0, run, end != NULL)) {
        memcpy(dst, ptr, run);
        dst += run;
        ptr += run;
        len = run;
        ci->readptr = ptr;
        ci->bytesinbuffer -= run;
    }
    else {
        // Too long. This is the path that has to report where it stopped, so
        // it still goes a character at a time and leaves the buffer where it
        // left off.
        unsigned int left = ci->bytesinbuffer;

        while(left) {
            char c;
            if((len++ == LINEMAX) && (*ptr != '\n')) {
                ci->bytesinbuffer = left;
                error(message[ERROR_LINETOOLONG],0);
                return 0;
            }
            left--;
            c = *ptr++;
            *dst++ = c;
            if(c == '\n') break;
        }
        ci->readptr = ptr;
        ci->bytesinbuffer = left;
    }

    *dst = 0;
    ci->filepos += len;
    ci->lastreadlength = len;
    return len;
}

// Get line from contentitem, copy it to dst
uint16_t getnextContentLine(char *dst, contentitem_t *ci) {

    if(completefilebuffering) {
        return _readFullBufferedLine(dst, ci);
    }
    else {
        return _readMinimumBufferedLine(dst, ci);
    }
}

uint16_t getlastContentLine(char *dst, contentitem_t *ci) {
    seekContentInput(ci, ci->filepos - ci->lastreadlength); // rewind
    return getnextContentLine(dst, ci);
}

uint8_t indexFromCPUtype(void) {
    switch(cputype) {
        case CPU_Z80: return 0;
        case CPU_Z180: return 1;
        case CPU_Z280: return 2;
        case CPU_EZ80: return 3;
        default: return 4;
    }
}

void errorCPUtype(errormessage_t index) {
    error(message[index], "%s", cpuname[indexFromCPUtype()]);
}
