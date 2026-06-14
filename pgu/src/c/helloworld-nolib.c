/* PURPOSE: Write the message "hello world" and exit, with no
 *          C library — direct system calls only.
 */

#include "os.h"

static const char helloworld[] = "hello world\n";

__attribute__((noreturn)) void _start(void) {
    os_write(STDOUT, helloworld, sizeof(helloworld) - 1);
    os_exit(0);
}
