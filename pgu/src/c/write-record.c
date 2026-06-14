/* PURPOSE: Write one record from the given buffer to fd. */

#include "os.h"
#include "record-def.h"

int write_record(int fd, const void *buffer) {
    return (int)os_write(fd, buffer, RECORD_SIZE);
}
