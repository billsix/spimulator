/* PURPOSE: Read one record from the given file descriptor into
 *          the supplied buffer.
 */

#include "os.h"
#include "record-def.h"

int read_record(int fd, void *buffer) {
    return (int)os_read(fd, buffer, RECORD_SIZE);
}
