/* PURPOSE: Print an error code and message to STDERR and exit
 *          with status 1.
 *
 * INPUT:   error_code - null-terminated short identifier
 *          error_msg  - null-terminated descriptive message
 */

#include "os.h"

extern int count_chars(const char *s);
extern void write_newline(int fd);

__attribute__((noreturn)) void error_exit(const char *error_code,
                                          const char *error_msg) {
    os_write(STDERR, error_code, count_chars(error_code));
    os_write(STDERR, error_msg, count_chars(error_msg));
    write_newline(STDERR);
    os_exit(1);
}
