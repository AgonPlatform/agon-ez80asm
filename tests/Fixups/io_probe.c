/* Test-only stdio instrumentation for io.c; never linked into the assembler. */
#undef main
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "io.h"

static unsigned int reads, writes;
static int failed;
static int fail(const char *operation, FILE *file) {
    const char *requested = getenv("EZ80_IO_FAIL");
    if(!failed && file == filehandle[FILE_OUTPUT] && requested && !strcmp(requested, operation)) {
        failed = 1;
        return 1;
    }
    return 0;
}
size_t probeRead(void *data, size_t size, size_t count, FILE *file) {
    if(file == filehandle[FILE_OUTPUT]) reads++;
    if(fail("read", file)) return 0;
    return fread(data, size, count, file);
}
size_t probeWrite(const void *data, size_t size, size_t count, FILE *file) {
    if(file == filehandle[FILE_OUTPUT]) writes++;
    if(fail("write", file)) return 0;
    return fwrite(data, size, count, file);
}
int probeSeek(FILE *file, long offset, int origin) {
    if(fail("seek", file)) return -1;
    return fseek(file, offset, origin);
}
int probeClose(FILE *file) {
    int inject = fail("close", file);
    int result = fclose(file);
    return inject ? EOF : result;
}
int assemblerMain(int argc, char **argv);
int main(int argc, char **argv) {
    int result = assemblerMain(argc, argv);
    printf("PROBE reads=%u writes=%u\n", reads, writes);
    return result;
}
