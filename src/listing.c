#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <ctype.h>
#include "config.h"
#include "defines.h"
#include "listing.h"
#include "globals.h"
#include "utils.h"
#include "io.h"
#include "assemble.h"

// Local variables
char     _listLine[LINEBUFFERSIZE];
uint24_t _listAddress;
uint8_t  _listObjects[LISTING_OBJECTS_PER_LINE];
uint8_t  _listLineObjectCount;
uint24_t  _listLineNumber;
uint24_t _listSourceLineNumber;

char _listHeader[]     = "PC     Output      Line\n\r";
char _listDataHeader[] = "       ";

char buffer[(LINEMAX * 2) + 1];

void listInit(void) {
    sprintf(buffer, "%s", _listHeader);
    if(listing) ioPuts(FILE_LISTING, buffer);
    _listLine[0] = 0;
}

void listStartLine(const char *line, unsigned int linenumber) {    
    strcpy(_listLine, line);
    trimRight(_listLine);
    _listAddress = address;
    _listLineObjectCount = 0;
    _listSourceLineNumber = currentExpandedMacro?_listSourceLineNumber:linenumber; // remember upstream linenumber during macro expansion
    _listLineObjectCount = 0;
    _listLineNumber = 0;
}

void listPrintDSLines(int number, int value) {
    while(number) {
        uint8_t i = 0;
        if(listing) ioPuts(FILE_LISTING, _listDataHeader);

        while(i < LISTING_OBJECTS_PER_LINE) {
            if(number) {
                sprintf(buffer, "%02X ",value);
                if(listing) ioPuts(FILE_LISTING, buffer);
                number--;
            }
            i++;
        }
        if(listing) ioPuts(FILE_LISTING, "\n");
    }
}

void listPrintLine(void) {
    uint8_t i,spaces;

    if(_listLineNumber == 0) {
        sprintf(buffer, "%06X ",_listAddress);
        if(listing) ioPuts(FILE_LISTING, buffer);
    }
    else {
        if(listing) ioPuts(FILE_LISTING, _listDataHeader);
    }
    for(i = 0; i < _listLineObjectCount; i++) {
        sprintf(buffer, "%02X ",_listObjects[i]);
        if(listing) ioPuts(FILE_LISTING, buffer);
    }
    spaces = LISTING_OBJECTS_PER_LINE - _listLineObjectCount;
    for(i = 0; i < spaces; i++) {
        sprintf(buffer, "   ");
        if(listing) ioPuts(FILE_LISTING, buffer);
    }
    if(_listLineNumber == 0) {
        sprintf(buffer, "%04d", currentExpandedMacro?macrolinenumber:_listSourceLineNumber);
        for(i = 1; i < contentlevel; i++) {
            strcat(buffer, "*");
        }
        if(currentExpandedMacro) {
            char tmpbuffer[6];
            snprintf(tmpbuffer, 6, "M%d ", macrolevel);
            strcat(buffer, tmpbuffer);
        }
        else strcat(buffer, "   ");

        for(i = MAXPROCESSDEPTH - i;i > 0; i--) {
            strcat(buffer, " ");
        }
        if(listing) ioPuts(FILE_LISTING, buffer);
        sprintf(buffer, "%s", _listLine);
        if(listing) ioPuts(FILE_LISTING, buffer);
    }

    if(listing) ioPuts(FILE_LISTING, "\n");

    _listLineObjectCount = 0;
    _listLineNumber++;
}

void listPrintComment(const char *src) {
        sprintf(buffer, "                       M%d %s\n", macrolevel, src);
        if(listing) ioPuts(FILE_LISTING, buffer);
}

void listEndLine(void) {
    if(_listLineNumber == 0) listPrintLine(); // unfinished first line
    if((_listLineNumber) && (_listLineObjectCount)) listPrintLine(); // unfinished last line
}

void listEmit8bit(uint8_t value) {
    if(_listLineObjectCount == LISTING_OBJECTS_PER_LINE) {
        listPrintLine();
    }
    _listObjects[_listLineObjectCount++] = value; 
}
// The listing is an optional disk spool. Correct its object columns from the
// patched binary before displaying it, without reading source files again.
void listFinish(void) {
    FILE *fh = filehandle[FILE_LISTING];
    char line[LINEMAX * 2 + 1];
    uint24_t offset = 0;
    if(fflush(fh) || fseek(fh, 0, SEEK_SET)) goto fail;
    while(true) {
        long position = ftell(fh), end;
        char *p;
        size_t length;
        bool objectline = true, changed = false;
        unsigned int i;
        if(!fgets(line, sizeof(line), fh)) break;
        end = ftell(fh);
        length = strlen(line);
        p = line;
        if(*p == '\r') p++;
        if(strlen(p) < 7) objectline = false;
        else {
            for(i = 0; i < 6; i++)
                if(p[i] != ' ' && !isxdigit((unsigned char)p[i])) objectline = false;
            if(p[6] != ' ') objectline = false;
        }
        if(objectline) {
            for(i = 0; i < LISTING_OBJECTS_PER_LINE; i++) {
                char *hex = p + 7 + 3*i;
                unsigned char byte;
                static const char digits[] = "0123456789ABCDEF";
                if(hex + 2 >= line + length || !isxdigit((unsigned char)hex[0]) ||
                   !isxdigit((unsigned char)hex[1]) || hex[2] != ' ') break;
                byte = ioReadOutputByte(offset++);
                if(errorcount) return;
                if(hex[0] != digits[byte >> 4] || hex[1] != digits[byte & 15]) changed = true;
                hex[0] = digits[byte >> 4];
                hex[1] = digits[byte & 15];
            }
        }
        if(consolelist_enabled) printf("%s", line);
        if(changed && (fseek(fh, position, SEEK_SET) || fwrite(line, 1, length, fh) != length ||
           fseek(fh, end, SEEK_SET))) goto fail;
    }
    if(ferror(fh)) goto fail;
    return;
fail:
    error(message[ERROR_FILEIO], "%s", filename[FILE_LISTING]);
}
