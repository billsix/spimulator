/* PURPOSE: Same "hello world" as helloworld-nolib, but written
 *          with the C standard library — printf and exit — so
 *          we link against libc and use the normal main() entry.
 */

#include <stdio.h>
#include <stdlib.h>

int main(void) {
    printf("hello world\n");
    exit(0);
}
