/* PURPOSE: Walk a heap of headered regions and grow the program
 *          break via SYS_BRK when more space is needed.  Pure
 *          translation of the book's alloc.s.
 *
 * Each region looks like:
 *
 *   ###########################################
 *   # available marker # size # actual bytes #
 *   ###########################################
 *                              ^-- pointer returned to caller
 *
 * Multi-arch via os.h — works on every Linux target supported
 * there (i386, x86_64, arm, aarch64, mips).
 */

#include "os.h"

#define HEADER_SIZE 8
#define HDR_AVAIL_OFFSET 0
#define HDR_SIZE_OFFSET 4

#define UNAVAILABLE 0
#define AVAILABLE 1

static unsigned long heap_begin = 0;
static unsigned long current_break = 0;

void allocate_init(void) {
    /* brk(0) returns the current program break. */
    unsigned long brk = (unsigned long)os_brk((void *)0);
    brk++;
    current_break = brk;
    heap_begin = brk;
}

void *allocate(unsigned size) {
    unsigned long ptr = heap_begin;

    while (ptr != current_break) {
        unsigned long avail = *(unsigned long *)(ptr + HDR_AVAIL_OFFSET);
        unsigned long rsize = *(unsigned long *)(ptr + HDR_SIZE_OFFSET);

        if (avail == AVAILABLE && size <= rsize) {
            *(unsigned long *)(ptr + HDR_AVAIL_OFFSET) = UNAVAILABLE;
            return (void *)(ptr + HEADER_SIZE);
        }
        ptr += HEADER_SIZE + rsize;
    }

    /* No fit — ask the kernel for more memory. */
    unsigned long new_break = current_break + HEADER_SIZE + size;
    unsigned long ret = (unsigned long)os_brk((void *)new_break);
    if (ret == 0) {
        return 0;
    }

    *(unsigned long *)(current_break + HDR_AVAIL_OFFSET) = UNAVAILABLE;
    *(unsigned long *)(current_break + HDR_SIZE_OFFSET) = size;

    void *result = (void *)(current_break + HEADER_SIZE);
    current_break = new_break;
    return result;
}

void deallocate(void *p) {
    unsigned long ptr = (unsigned long)p - HEADER_SIZE;
    *(unsigned long *)(ptr + HDR_AVAIL_OFFSET) = AVAILABLE;
}
