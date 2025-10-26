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

// File basename variable
char filebasename[FILENAMEMAXLENGTH + 1];

// Global variables
char     filename[OUTPUTFILES][FILENAMEMAXLENGTH + 1];
FILE*    filehandle[OUTPUTFILES];
contentitem_t *filecontent[256]; // hash table with all file content items

// Local variables
char *   _bufferstart[OUTPUTFILES];          // statically set start of buffer to each file
char *   _filebuffer[OUTPUTFILES];            // actual moving pointers in buffer
uint24_t _filebuffersize[OUTPUTFILES];        // current fill size of each buffer
bool     _fileEOF[OUTPUTFILES];
char     _outputbuffer[OUTPUT_BUFFERSIZE];

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

void _initFileBuffers(void) {
    int n;

    for(n = 0; n < OUTPUTFILES; n++) _bufferstart[n] = 0;
    _bufferstart[FILE_OUTPUT] = _outputbuffer;

    for(n = 0; n < OUTPUTFILES; n++) {
        _filebuffer[n] = _bufferstart[n];
        _filebuffersize[n] = 0;
        _fileEOF[n] = false;
    }
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

    strcpy(filename[FILE_ANONYMOUS_LABELS], filebasename);
    strcpy(filename[FILE_LISTING], filebasename);
    strcat(filename[FILE_ANONYMOUS_LABELS], ".lbl");
    strcat(filename[FILE_LISTING], ".lst");
}

void _deleteFiles(void) {
    if(CLEANUPFILES) {
        remove(filename[FILE_ANONYMOUS_LABELS]);
    }
    if(errorcount && CLEANUPFILES) remove(filename[FILE_OUTPUT]);
}

void _closeAllFiles(void) {
    if(filehandle[FILE_OUTPUT]) fclose(filehandle[FILE_OUTPUT]);
    if(filehandle[FILE_ANONYMOUS_LABELS]) fclose(filehandle[FILE_ANONYMOUS_LABELS]);
    if(list_enabled && filehandle[FILE_LISTING]) fclose(filehandle[FILE_LISTING]);
}

bool _openfiles(void) {
    if(!_openFile(FILE_OUTPUT, "wb+")) {
        error("Error creating output file", 0);
        _closeAllFiles();
        return false;
    }
    if(!_openFile(FILE_ANONYMOUS_LABELS, "wb+")) {
        error("Error creating anonymous labels file", 0);
        _closeAllFiles();
        return false;
    }
    if(list_enabled) {
        if(!_openFile(FILE_LISTING, "w")) {
            error("Error creating listing file", 0);
            _closeAllFiles();
            return false;
        }
    }
    return true;
}

// Will be called for output files only
// These files will have a buffer set up previously
void _io_flush(uint8_t fh) {
    fwrite(_bufferstart[fh], 1, _filebuffersize[fh], filehandle[fh]);
    _filebuffer[fh] = _bufferstart[fh];
    _filebuffersize[fh] = 0;
}

// Flush all output files
void _io_flushOutput(void) {
    for(int fh = 0; fh < OUTPUTFILES; fh++) {
        if(_bufferstart[fh]) _io_flush(fh);
    }
}

// Only called on output-mode files
void ioPutc(uint8_t fh, unsigned char c) {
    if(_bufferstart[fh]) {
        // Buffered IO
        *(_filebuffer[fh]++) = c;
        _filebuffersize[fh]++;
        if(_filebuffersize[fh] == OUTPUT_BUFFERSIZE) _io_flush(fh);
    }
    else fputc(c, filehandle[fh]); // regular non-buffered IO
}

void io_outputc(unsigned char c) {
    *(_filebuffer[FILE_OUTPUT]++) = c;
    _filebuffersize[FILE_OUTPUT]++;
    if(_filebuffersize[FILE_OUTPUT] == OUTPUT_BUFFERSIZE) _io_flush(FILE_OUTPUT);
}

void  ioWrite(uint8_t fh, const char *s, uint16_t size) {
    if(_bufferstart[fh]) {
        // Buffered IO
        while(size--) {
            *(_filebuffer[fh]++) = *s++;
            _filebuffersize[fh]++;
            if(_filebuffersize[fh] == OUTPUT_BUFFERSIZE) _io_flush(fh);
        }
    }
    else fwrite(s, 1, size, filehandle[fh]);
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

bool ioInit(const char *input_filename, const char *output_filename) {
    create_filebasename(input_filename);
    _prepare_filenames(output_filename);
    _initFileBuffers();
    return _openfiles();
}

void ioClose(void) {
    _io_flushOutput();
    _closeAllFiles();
    _deleteFiles();
}

void emit_8bit(uint8_t value) {
    if(pass == ENDPASS) {
        if(remaining_dsspaces) {
            if(listing) listPrintDSLines(remaining_dsspaces, fillbyte);
            while(remaining_dsspaces) {
                io_outputc(fillbyte);
                remaining_dsspaces--;
            }
        }
        if(listing) listEmit8bit(value);
        io_outputc(value);
    }
    address++;
}

void emit_16bit(uint16_t value) {
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
    emit_8bit(op->immediate & 0xFF);
    emit_8bit((op->immediate >> 8) & 0xFF);
    if(num == 2) validateRange16bit(op->immediate, op->immediate_name);
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
        ci->bytesinbuffer = 0; // reset buffer
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
