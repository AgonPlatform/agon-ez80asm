#ifndef IO_H
#define IO_H

#include "config.h"
#include "defines.h"

// Global variables
extern char filebasename[FILENAMEMAXLENGTH + 1];
extern bool list_enabled;
extern bool consolelist_enabled;
extern char filename[OUTPUTFILES][FILENAMEMAXLENGTH + 1];    // 0 - binary output, 3 - anonymous labels, 4 - listing
extern FILE* filehandle[OUTPUTFILES];
extern contentitem_t *filecontent[256]; // hash table with all file content items

FILE *ioOpenfile(const char *name, const char *mode);
uint24_t ioGetfilesize(FILE *fh);
void ioWrite(uint8_t fh, const char *s, uint24_t size);
bool ioInit(const char *input_filename, const char *output_filename); // init - called once at start
void ioClose(void);                                // close everything at end, do cleanup
void ioPutc(uint8_t fh, unsigned char c);          // buffered write of a single byte / fallback
int  ioPuts(uint8_t fh, const char *s);                  // buffered write of a string / fallback
void emit_8bit(uint8_t value);
void emit_16bit(uint16_t value);
void emit_24bit(uint24_t value);
void emit_32bit(uint32_t value);
void emit_quotedstring(const char *str);
void emit_adlsuffix_code(uint8_t suffix);
void emit_immediate(const operand_t *op, uint8_t suffix);
void initFileContentTable(void);

void openContentInput(contentitem_t *ci, char *buffer);
void closeContentInput(contentitem_t *ci, contentitem_t *callerci);
void seekContentInput(contentitem_t *ci, uint24_t position); // position relative to start of input

#endif // IO_H
