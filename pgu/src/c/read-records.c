/* PURPOSE: Open test.dat, read records, print each first name to
 *          STDOUT followed by a newline.
 *
 * Links against read-record.c, count-chars.c, write-newline.c.
 */

#include "os.h"
#include "record-def.h"

extern int read_record(int fd, void *buffer);
extern int count_chars(const char *s);
extern void write_newline(int fd);

static struct record record_buffer;

__attribute__((noreturn)) void _start(void) {
    static const char file_name[] = "test.dat";
    int fd_in = (int)os_open(file_name, OS_O_RDONLY, 0666);
    int fd_out = STDOUT;

    while (1) {
        int n = read_record(fd_in, &record_buffer);
        if (n != RECORD_SIZE) {
            break;
        }

        os_write(fd_out, record_buffer.firstname,
                 count_chars(record_buffer.firstname));
        write_newline(fd_out);
    }

    os_exit(0);
}
