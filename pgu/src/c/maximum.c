/* PURPOSE: Find the largest value in a null-terminated array of
 *          ints and return it as the exit status.
 */

#include "os.h"

static int data_items[] = {3,  67, 34, 222, 45, 75, 54,
                           34, 44, 33, 22,  11, 66, 0};

int maximum(void) {
    int i = 0;
    int max = data_items[0];

    while (data_items[i] != 0) {
        if (data_items[i] > max) {
            max = data_items[i];
        }
        i++;
    }
    return max;
}

__attribute__((noreturn)) void _start(void) {
    int max = maximum();
    os_exit(max);
}
