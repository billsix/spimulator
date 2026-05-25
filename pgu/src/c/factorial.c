/* PURPOSE: Recursive factorial of 4.  Exit status is the result.
 *          `./factorial; echo $?` prints 24.
 */

#include "os.h"

int factorial(int n) {
    if (n == 1) {
        return 1;
    }
    return n * factorial(n - 1);
}

__attribute__((noreturn)) void _start(void) { os_exit(factorial(4)); }
