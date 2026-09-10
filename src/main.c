#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#include "config.h"
#include "defines.h"
#include "console.h"
#include "getopt.h"
#include "globals.h"
#include "utils.h"
#include "assemble.h"
#include "label.h"
#include "macro.h"
#include "io.h"
#include "str2num.h"
#include "instruction.h"

char inputfilename[FILENAMEMAXLENGTH + 1];
char outputfilename[FILENAMEMAXLENGTH + 1];
bool noaction;

void printVersion(void) {
    printf("ez80asm version %d.%d, (C)2025 - Jeroen Venema\n",VERSION,REVISION);
}

void printHelp(void) {
    printf("Usage: ez80asm <filename> [output filename] [OPTION]\n\n");
    printf("  -v\tList version information only\n");
    printf("  -h\tList help information\n");
    printf("  -o\tOrg start address in hexadecimal format, default is %06X\n", START_ADDRESS);
    printf("  -b\tFillbyte in hexadecimal format, default is %02X\n", FILLBYTE);
    printf("  -a\tADL mode 1/0, default is %d\n", ADLMODE_START);
    printf("  -i\tIgnore value truncation warnings\n");
    printf("  -l\tListing to file with .lst extension\n");
    printf("  -s\tExport symbols\n");
    printf("  -d\tDirect listing to console\n");
    printf("  -c\tNo color codes in output\n");
    printf("  -x\tDisplay assembly statistics\n");
    printf("  -m\tAccepted and ignored, kept so older command lines still run\n");
    printf("\n");
}

void displayStatistics(void) {
    int outputsize;

    FILE *fh = ioOpenfile(filename[FILE_OUTPUT], "rb");
    if(fh == 0) outputsize = 0;
    else {
        outputsize = ioGetfilesize(fh);
        fclose(fh);
    }
    printf("\nAssembly statistics\n=============================\nLabel memory         : %6d\nLabels               : %6d\n\nMacro memory         : %6d\nMacros               : %6d\n\nInput buffers        : %6d\n-----------------------------\nTotal dynamic memory : %6d\n\nSources parsed       : %6d\nBinfiles read        : %6d\n\nOutput size          : %6d\n\n", labelmemsize, getGlobalLabelCount(), macromemsize, macroCounter, filecontentsize, labelmemsize+macromemsize+filecontentsize, sourcefilecount, binfilecount, outputsize);
}

void parseOptions(int argc, char *argv[]) {
    int opt;
    int filenamecount = 0;

    while ((opt = getopt(argc, argv, "-:lidvhsxcmb:a:o:")) != -1) {
        switch(opt) {
            case 'a':
                if((strlen(optarg) != 1) || 
                   ((*optarg != '0') && (*optarg != '1'))) {
                    error("Incorrect ADL mode option -a",0);
                    return;
                }
                adlmode = (*optarg == '1')?true:false;
                printf("Setting ADL mode to %d\n",adlmode);
                break;
            case 's':
                printf("Exporting symbols\n");
                exportsymbols = true;
                break;
            case 'x':
                displaystatistics = true;
                break;
            case 'c':
                coloroutput = false;
                break;
            case 'd':
                consolelist_enabled = true;
                break;
            case 'm':
                // Accepted and ignored. Source files are always read a buffer
                // at a time now; there is no other mode to ask for.
                break;
            case 'l':
                list_enabled = true;
                break;
            case 'i':
                ignore_truncation_warnings = true;
                break;
            case 'v':
                printVersion();
                noaction = true;
                return;
            case 'h':
                printHelp();
                noaction = true;
                return;
            case 'b':
                if(strlen(optarg) > 2) {
                    error("option -b: Byte range error",0);
                    return;
                }
                fillbyte = str2hex(optarg);
                if(err_str2num) {
                    error("option -b: Invalid hexadecimal format",0);
                    return;
                }
                printf("Setting fillbyte to hex %02X\n", fillbyte);
                break;
            case 'o':
                if(strlen(optarg) > 6) {
                    error("option -o: Address longer than 24bit",0);
                    return;
                }
                start_address = str2hex(optarg);
                if(err_str2num) {
                    error("option -o: Invalid hexadecimal format",0);
                    return;
                }
                printf("Setting org address to hex %06X\n", start_address);
                break;
            case '?':
                switch(optopt) {
                    case 'b':
                        error("option -b: Missing fillbyte value",0);
                        break;
                    case 'a':
                        error("option -a: Missing ADL mode value",0);
                        break;
                    case 'o':
                        error("option -o: Missing start address value",0);
                        break;
                    default:
                        error("Unknown option", "%c", optopt);
                        break;
                }
                return;
            case 1:
                if(strlen(optarg) > FILENAMEMAXLENGTH) {
                    error("Filename too long",0);
                    return;
                }
                filenamecount++;
                switch(filenamecount) {
                    case 1:
                        strcpy(inputfilename, optarg);
                        break;
                    case 2:
                        strcpy(outputfilename, optarg);
                        break;
                    default:
                        error("Too many filenames provided",0);
                        return;
                        break;
                }
                break;
        }
    }

    if((argc == 1) || (filenamecount == 0)) {
        error("No input filename",0);
        printHelp();
        return;
    }
}

int main(int argc, char *argv[]) {
    clock_t begin, end;

    // set option defaults
    noaction = false;
    fillbyte = FILLBYTE;
    list_enabled = false;
    consolelist_enabled = false;
    adlmode = ADLMODE_START;
    start_address = START_ADDRESS;
    exportsymbols = false;
    displaystatistics = false;
    coloroutput = true;
    ignore_truncation_warnings = false;

    parseOptions(argc, argv);

    if(noaction) return 0;
    if(errorcount) return EXIT_ERROR;

    if(!ioInit(inputfilename, outputfilename)) return EXIT_ERROR;
    
    printf("Assembling %s\n", inputfilename);
    if(list_enabled) printf("Listing to %s\n", filename[FILE_LISTING]);

    // Initializations
    initInstructionTable();
    initGlobalLabelTable();
    initMacros();
    initFileContentTable();
    sourcefilecount = 0;
    binfilecount = 0;
    errorcount = 0;
    errorreportlevel = 0;
    maxstackdepth = 0;
    macroexpansions = 0;
    cputype = CPU_EZ80;
    listing = list_enabled || consolelist_enabled;
    
    // Assemble input to output
    begin = clock();
    assemble(inputfilename);
    end = clock();

    ioClose();

    if(errorcount) return EXIT_ERROR;
    else printf("Done in %.2f seconds\n",((double)(end - begin) / CLOCKS_PER_SEC));

    if(exportsymbols) saveGlobalLabelTable();
    if(displaystatistics) displayStatistics();

    return EXIT_SUCCESS;
}
