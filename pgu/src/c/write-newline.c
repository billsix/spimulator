/* PURPOSE: Write a single newline character to the given fd. */

#include "os.h"

void write_newline(int fd) {
    static const char newline = '\n';
    os_write(fd, &newline, 1);
}
