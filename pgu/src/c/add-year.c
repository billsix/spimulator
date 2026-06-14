/* PURPOSE: Read every record from test.dat, increment the age
 *          field, and write the modified record to testout.dat.
 *
 * Links against read-record.c and write-record.c.
 */

#include "os.h"
#include "record-def.h"

extern int read_record(int fd, void *buffer);
extern int write_record(int fd, const void *buffer);

static struct record record_buffer;

__attribute__((noreturn)) void _start(void) {
    static const char input_file_name[] = "test.dat";
    static const char output_file_name[] = "testout.dat";

    int fd_in = (int)os_open(input_file_name, OS_O_RDONLY, 0666);
    int fd_out = (int)os_open(output_file_name, OS_O_WRONLY | OS_O_CREAT, 0666);

    while (1) {
        int n = read_record(fd_in, &record_buffer);
        if (n != RECORD_SIZE) {
            break;
        }

        record_buffer.age++;

        write_record(fd_out, &record_buffer);
    }

    os_exit(0);
}
