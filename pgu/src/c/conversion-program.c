/* PURPOSE: Convert the constant 824 to a decimal string and
 *          write it to stdout.  Demo for integer2string.
 *
 * Links against count-chars.c, integer-to-string.c, write-newline.c.
 */

#include "os.h"

extern int count_chars(const char *s);
extern void integer2string(int value, char *buffer);
extern void write_newline(int fd);

static char tmp_buffer[16];

__attribute__((noreturn)) void _start(void) {
    integer2string(824, tmp_buffer);
    int len = count_chars(tmp_buffer);

    os_write(STDOUT, tmp_buffer, len);
    write_newline(STDOUT);
    os_exit(0);
}
