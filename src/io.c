#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdint.h>
#include <stdbool.h>
#include "config.h"
#include "defines.h"
#include "globals.h"
#include "listing.h"
#include "macro.h"
#include "utils.h"
#include "moscalls.h"
#include "io.h"
#include "instruction.h"
#include "fixup.h"

// File basename variable
char filebasename[FILENAMEMAXLENGTH + 1];

// Global variables
char     filename[OUTPUTFILES][FILENAMEMAXLENGTH + 1];
FILE*    filehandle[OUTPUTFILES];
contentitem_t *filecontent[256]; // hash table with all file content items

// One fixed window is shared by sequential emission and random-access fixups.
static unsigned char outputBuffer[OUTPUT_BUFFERSIZE];
static uint24_t windowStart, windowUsed, outputSize;
static bool windowDirty;

uint24_t ioOutputPosition(void) { return outputSize; }

static bool flushWindow(void) {
    if(!windowDirty) return true;
    if(fseek(filehandle[FILE_OUTPUT], windowStart, SEEK_SET) ||
       fwrite(outputBuffer, 1, windowUsed, filehandle[FILE_OUTPUT]) != windowUsed) {
        error(message[ERROR_FILEIO], "%s", filename[FILE_OUTPUT]);
        return false;
    }
    windowDirty = false;
    return true;
}

static bool selectWindow(uint24_t position) {
    uint24_t start, length;
    if(position >= windowStart && position - windowStart < windowUsed) return true;
    if(!flushWindow()) return false;
    start = (position / OUTPUT_BUFFERSIZE) * OUTPUT_BUFFERSIZE;
    length = outputSize - start;
    if(length > OUTPUT_BUFFERSIZE) length = OUTPUT_BUFFERSIZE;
    if(fseek(filehandle[FILE_OUTPUT], start, SEEK_SET) ||
       fread(outputBuffer, 1, length, filehandle[FILE_OUTPUT]) != length) {
        error(message[ERROR_FILEIO], "%s", filename[FILE_OUTPUT]);
        return false;
    }
    windowStart = start;
    windowUsed = length;
    return true;
}

unsigned char ioReadOutputByte(uint24_t position) {
    if(position >= outputSize) { error(message[ERROR_INTERNAL], 0); return 0; }
    if(!selectWindow(position)) return 0;
    return outputBuffer[position - windowStart];
}

void ioPatchByte(uint24_t position, unsigned char value) {
    if(position >= outputSize) { error(message[ERROR_INTERNAL], 0); return; }
    if(!selectWindow(position)) return;
    outputBuffer[position - windowStart] = value;
    windowDirty = true;
}

/* Select once per field. The byte fallback handles fields crossing windows. */
void ioPatchValue(uint24_t position, const int32_t *value, uint8_t width) {
    unsigned char bytes[4];
    unsigned char *dst;
    if(width < 1 || width > 4 || position > outputSize || width > outputSize - position) {
        error(message[ERROR_INTERNAL], 0);
        return;
    }
    bytes[0] = (uint8_t)*value;
    bytes[1] = (uint8_t)((uint32_t)*value >> 8);
    bytes[2] = (uint8_t)((uint32_t)*value >> 16);
    bytes[3] = (uint8_t)((uint32_t)*value >> 24);
    if(!selectWindow(position)) return;
    if(width <= windowUsed - (position - windowStart)) {
        dst = outputBuffer + (position - windowStart);
        dst[0] = bytes[0];
        if(width > 1) dst[1] = bytes[1];
        if(width > 2) dst[2] = bytes[2];
        if(width > 3) dst[3] = bytes[3];
        windowDirty = true;
    }
    else {
        uint8_t i;
        for(i = 0; i < width && !errorcount; i++) ioPatchByte(position + i, bytes[i]);
    }
}

/* No early flush: preserve aligned windows and the exact-64-KiB resident case.
 * Sixteen bytes bound every encoding supported by the instruction tables.
 * Listings and boundary instructions retain the ordinary byte emitter. */
unsigned char *ioReserveInstruction(void) {
    if(listing || errorcount) return NULL;
    if(remaining_dsspaces) ioFlushDSSpaces();
#if OUTPUT_BUFFERSIZE < 16
    return NULL; // Small test windows always use the ordinary byte emitter.
#else
    // Compare against a constant: subtracting windowUsed from the buffer size
    // promotes the old expression to expensive 32-bit arithmetic on eZ80.
    if(errorcount || windowUsed > OUTPUT_BUFFERSIZE - 16) return NULL;
#endif
    return outputBuffer + windowUsed;
}

void ioCommitInstruction(uint8_t length) {
    windowUsed += length;
    outputSize += length;
    address += length;
    windowDirty = true;
}

#ifdef AGONDEV
    // platform-specific for Agon AGONDEV
    int remove(const char *filename) {
        return removefile(filename);
    }
#endif // else use standard remove()

FILE *ioOpenfile(const char *name, const char *mode) {
    FILE *fh = fopen(name, mode);
    if(!fh) {
        error("Error opening", "%s", name);
    }
    return fh;
}

uint24_t ioGetfilesize(FILE *fh) {
    uint24_t filesize;

    #ifdef AGONDEV
        // Use optimal assembly routine in moscalls.asm
        filesize = getfilesize(fh->fhandle);
    #else
        fseek(fh, 0, SEEK_END);
        filesize = ftell(fh);
        fseek(fh, 0, SEEK_SET);
    #endif

    return filesize;
}

// opens a file a places the result at the file pointer
bool _openFile(uint8_t filenumber, const char* mode) {
    FILE* file = fopen(filename[filenumber], mode);
    filehandle[filenumber] = file;
    if(file) return true;
    else return false;
}

void create_filebasename(const char *input_filename) {
    strcpy(filebasename, input_filename);
    remove_ext(filebasename, '.', '/');
}

// Prepare filenames according to input filename
// If output_filename is given, adopt that, 
// otherwise append base inputfilename + .bin
void _prepare_filenames(const char *output_filename) {
    // prepare filenames
    if((output_filename == NULL) || (strlen(output_filename) == 0)) {
        strcpy(filename[FILE_OUTPUT], filebasename);
        strcat(filename[FILE_OUTPUT], ".bin");
    }
    else {
        strcpy(filename[FILE_OUTPUT], output_filename);
    }

    strcpy(filename[FILE_LISTING], filebasename);
    strcat(filename[FILE_LISTING], list_enabled ? ".lst" : ".lst.tmp");
}

void _deleteFiles(void) {
    if(CLEANUPFILES) {
        if(!list_enabled && consolelist_enabled) remove(filename[FILE_LISTING]);
    }
    if(errorcount && CLEANUPFILES) remove(filename[FILE_OUTPUT]);
}

void _closeAllFiles(void) {
    if(filehandle[FILE_OUTPUT] && fclose(filehandle[FILE_OUTPUT])) error(message[ERROR_FILEIO], "%s", filename[FILE_OUTPUT]);

    if(filehandle[FILE_LISTING] && fclose(filehandle[FILE_LISTING])) error(message[ERROR_FILEIO], "%s", filename[FILE_LISTING]);
}

bool _openfiles(void) {
    if(!_openFile(FILE_OUTPUT, "wb+")) {
        error("Error creating output file", 0);
        _closeAllFiles();
        return false;
    }
    if(list_enabled || consolelist_enabled) {
        if(!_openFile(FILE_LISTING, "wb+")) {
            error("Error creating listing file", 0);
            _closeAllFiles();
            return false;
        }
    }
    return true;
}

// Append whole chunks; a full window stays resident until another byte arrives.
void ioWrite(uint8_t fh, const char *s, uint24_t size) {
    if(fh != FILE_OUTPUT) {
        if(fwrite(s, 1, size, filehandle[fh]) != size) error(message[ERROR_FILEIO], "%s", filename[fh]);
        return;
    }
    while(size && !errorcount) {
        uint24_t run;
        if(windowUsed == OUTPUT_BUFFERSIZE) {
            if(!flushWindow()) return;
            windowStart = outputSize;
            windowUsed = 0;
        }
        run = OUTPUT_BUFFERSIZE - windowUsed;
        if(run > size) run = size;
        memcpy(outputBuffer + windowUsed, s, run);
        windowUsed += run;
        outputSize += run;
        windowDirty = true;
        s += run;
        size -= run;
    }
}

void ioPutc(uint8_t fh, unsigned char c) { ioWrite(fh, (const char *)&c, 1); }

void io_outputfill(unsigned char c, uint24_t count) {
    while(count && !errorcount) {
        uint24_t run;
        if(windowUsed == OUTPUT_BUFFERSIZE) {
            if(!flushWindow()) return;
            windowStart = outputSize;
            windowUsed = 0;
        }
        run = OUTPUT_BUFFERSIZE - windowUsed;
        if(run > count) run = count;
        memset(outputBuffer + windowUsed, c, run);
        windowUsed += run;
        outputSize += run;
        windowDirty = true;
        count -= run;
    }
}

int ioPuts(uint8_t fh, const char *s) {
    int number = 0;
    while(*s) {
        ioPutc(fh, *s);
        number++;
        s++;
    }
    return number;
}

bool ioInit(const char *input_filename, char *output_filename) {
    create_filebasename(input_filename);
    _prepare_filenames(output_filename);
    windowStart = windowUsed = outputSize = 0;
    windowDirty = false;
    strcpy(output_filename, filename[FILE_OUTPUT]);
    return _openfiles();
}

void ioClose(void) {
    if(filehandle[FILE_OUTPUT] && !errorcount) flushWindow();
    _closeAllFiles();
    _deleteFiles();
}

void ioFlushDSSpaces(void) {

    if(listing && remaining_dsspaces) listPrintDSLines(remaining_dsspaces, fillbyte);
    if(remaining_dsspaces) {
        io_outputfill(fillbyte, remaining_dsspaces);
        remaining_dsspaces = 0;
    }
}

void emit_8bit(uint8_t value) {
    // Keep the common single-byte emission path inline.
    if(remaining_dsspaces) ioFlushDSSpaces();
    if(listing) listEmit8bit(value);
    if(windowUsed == OUTPUT_BUFFERSIZE) {
        if(!flushWindow()) return;
        windowStart = outputSize;
        windowUsed = 0;
    }
    outputBuffer[windowUsed++] = value;
    outputSize++;
    windowDirty = true;
    address++;
}

void emit_16bit(uint24_t value) {
    emit_8bit(value&0xFF);
    emit_8bit((value>>8)&0xFF);
}

void emit_24bit(uint24_t value) {
    emit_8bit(value&0xFF);
    emit_8bit((value>>8)&0xFF);
    emit_8bit((value>>16)&0xFF);
}

void emit_32bit(uint32_t value) {
    emit_8bit(value&0xFF);
    emit_8bit((value>>8)&0xFF);
    emit_8bit((value>>16)&0xFF);
    emit_8bit((value>>24)&0xFF);
}

void emit_adlsuffix_code(uint8_t suffix) {
    uint8_t code;
    switch(suffix) {
        case S_SIS:
            code = CODE_SIS;
            break;
        case S_LIS:
            code = CODE_LIS;
            break;
        case S_SIL:
            code = CODE_SIL;
            break;
        case S_LIL:
            code = CODE_LIL;
            break;
        default:
            error(message[ERROR_INVALIDSUFFIX],0);
            return;
    }
    emit_8bit(code);
}

// emits a string surrounded by literal string quotes, as the token gets in from a file
// Only called when the first character is a double quote
void emit_quotedstring(const char *str) {
    bool escaped = false;
    uint8_t escaped_char;

    str++; // skip past first "
    while(*str) {
        if(!escaped) {
            if(*str == '\\') { // escape character
                escaped = true;
            }
            else {
                if(*str == '\"') return;
                else emit_8bit(*str);
            }
        }
        else { // previously escaped
            escaped_char = getEscapedChar(*str);
            if(escaped_char == 0xff) {
                error(message[ERROR_ILLEGAL_ESCAPESEQUENCE],0);
                return;
            }
            emit_8bit(escaped_char);
            escaped = false;
        }
        str++;
    }
    // we missed an end-quote to this string, we shouldn't reach this
    error(message[ERROR_STRING_NOTTERMINATED],0);
}

// Emit a 16 or 24 bit immediate number, according to
// given suffix bit, or in lack of it, the current ADL mode
void emit_immediate(const operand_t *op, uint8_t suffix) {
    uint8_t num;

    num = get_immediate_size(suffix);
    if(op->fixup) attachFixup(op->fixup, num, 1, 0);
    emit_8bit(op->immediate & 0xFF);
    emit_8bit((op->immediate >> 8) & 0xFF);
    if(num == 2) validateRange16bit(&op->immediate, op->immediate_name);
    if(num == 3) emit_8bit((op->immediate >> 16) & 0xFF);
}

void initFileContentTable(void) {
    filecontentsize = 0;
    memset(filecontent, 0, sizeof(filecontent));
}

// sets read position in input stream
// Will be called after 'prepareContentInput', before 'closeContentInput'
void seekContentInput(contentitem_t *ci, uint24_t position) {
    ci->filepos = position;

    if(completefilebuffering) {
        ci->readptr = ci->buffer + position;
    }
    else {
        // Reset the buffer, the carried partial line with it: the seek is
        // absolute, so nothing already read is worth keeping.
        ci->bytesinbuffer = 0;
        ci->rawinbuffer = 0;
        ci->readptr = ci->buffer;
        if(fseek(ci->fh, position, SEEK_SET)) {
            error(message[ERROR_FILEIO],"%s",ci->name);
            return;
        }
    }
}

void openContentInput(contentitem_t *ci, char *buffer) {
    if(!completefilebuffering) {
        ci->buffer = buffer;
        ci->bytesinbuffer = 0;
        ci->rawinbuffer = 0;
        ci->fh = ioOpenfile(ci->name, "rb");
        if(ci->fh == 0) return;
        ci->size = ioGetfilesize(ci->fh);
    }
    ci->currentlinenumber = 0;
    ci->inConditionalSection = inConditionalSection;
    ci->readptr = ci->buffer;
    ci->lastreadlength = 0;
    ci->filepos = 0;

    currentcontentitem = ci;
    inConditionalSection = CONDITIONSTATE_NORMAL;
}

void closeContentInput(contentitem_t *ci, contentitem_t *callerci) {
    if(!completefilebuffering) {    
        ci->buffer = NULL;
        ci->bytesinbuffer = 0;
        ci->size = 0;
        fclose(ci->fh);
    }
    ci->filepos = 0;
    ci->readptr = NULL;

    currentcontentitem = callerci;
    inConditionalSection = ci->inConditionalSection;
}
