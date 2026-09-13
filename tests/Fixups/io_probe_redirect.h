/* Include libc declarations before redirecting calls in io.c. In particular,
 * do not rename fortified libc inline definitions into probe functions. */
#include <stdio.h>

size_t probeRead(void *, size_t, size_t, FILE *);
size_t probeWrite(const void *, size_t, size_t, FILE *);
int probeSeek(FILE *, long, int);
int probeClose(FILE *);

#undef fread
#undef fwrite
#undef fseek
#undef fclose
#define fread probeRead
#define fwrite probeWrite
#define fseek probeSeek
#define fclose probeClose
