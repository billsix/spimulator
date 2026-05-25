/* PURPOSE: Compute 2^3 + 5^2 by calling power() twice and
 *          returning the sum as the exit status.
 *
 * NOTES: The power must be 1 or greater.
 */

#include "os.h"

int power(int base, int pow) {
    int result = base;
    while (pow > 1) {
        result = result * base;
        pow--;
    }
    return result;
}

__attribute__((noreturn)) void _start(void) {
    int a = power(2, 3);
    int b = power(5, 2);
    os_exit(a + b);
}
