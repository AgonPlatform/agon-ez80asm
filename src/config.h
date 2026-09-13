#ifndef CONFIG_H
#define CONFIG_H

#ifdef _MSC_VER
#define strncasecmp _strnicmp
#define strcasecmp _stricmp
#pragma warning(disable:4996)			// 'strcpy': This function or variable may be unsafe. Consider using strcpy_s instead
#pragma warning(disable:4267)			// conversion from 'size_t' to 'int', possible loss of data
#pragma warning(disable:4244)			// conversion from '__int64' to 'uint24_t', possible loss of data
#endif

#define VERSION                       2
#define REVISION                      2
#define ADLMODE_START              true
#define START_ADDRESS           0x40000 // Agon default load address
#define FILLBYTE                   0xFF // Same as ZDS
#define INSTRUCTION_HASHTABLESIZE   256 // Number of entries in the hashtable
#define GLOBAL_LABEL_TABLE_SIZE     256
#define MAXPROCESSDEPTH               8 // Maximum simultaneous processing 'depth' of files / include files
#define MACRO_MAXLEVEL                8 // Maximum depth level of recursive macro calling
#define LINEMAX                     256 // Maximum bytes before LF in an input line
#define LINEBUFFERSIZE             (LINEMAX + 2) // Optional LF and terminating zero
#define FILENAMEMAXLENGTH            64
#define OUTPUTFILES                   2 // Output files (binary / optional listing spool)
#ifndef OUTPUT_BUFFERSIZE
#define OUTPUT_BUFFERSIZE         65536UL // Fixed binary output window, independent of -m
#endif
#if OUTPUT_BUFFERSIZE < 1
#error OUTPUT_BUFFERSIZE must be positive
#endif
#define INPUT_BUFFERSIZE          16384 // For minimally buffered input files
#define LISTING_OBJECTS_PER_LINE      4 // Listing hex 'objects' between PC / Line number
#define TOKEN_MAX               LINEMAX // Token maximum length
#define MAXNAMELENGTH                64 // Maximum name length of labels
#define MACROMAXARGS                  8 // Maximum arguments to a macro
#define MACROARGLENGTH               64
#define MAX_MNEMONIC_SIZE            32
#define MACROARGSUBSTITUTIONLENGTH  FILENAMEMAXLENGTH+2 // Maximum length of macro argument, accounting for passing filename with double quotes
#define MACROLINEMAX                MACROARGSUBSTITUTIONLENGTH * MACROMAXARGS + LINEMAX
#define CLEANUPFILES               true
#endif // CONFIG_H
