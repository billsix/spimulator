/* PURPOSE: Create test.dat and write three fixed records.
 *
 * Designated initializers zero-fill any unspecified bytes in
 * each fixed-size char array, matching the .rept padding of
 * the original asm version.
 */

#include "os.h"
#include "record-def.h"

extern int write_record(int fd, const void *buffer);

static const struct record records[3] = {
    {
        .firstname = "Fredrick",
        .lastname = "Bartlett",
        .address = "4242 S Prairie\nTulsa, OK 55555",
        .age = 45,
    },
    {
        .firstname = "Marilyn",
        .lastname = "Taylor",
        .address = "2224 S Johannan St\nChicago, IL 12345",
        .age = 29,
    },
    {
        .firstname = "Derrick",
        .lastname = "McIntire",
        .address = "500 W Oakland\nSan Diego, CA 54321",
        .age = 36,
    },
};

__attribute__((noreturn)) void _start(void) {
    static const char file_name[] = "test.dat";
    int fd = (int)os_open(file_name, OS_O_WRONLY | OS_O_CREAT, 0666);

    for (int i = 0; i < 3; i++) {
        write_record(fd, &records[i]);
    }

    os_close(fd);
    os_exit(0);
}
