/* PURPOSE: Demonstrates calling printf with a format string and
 *          several arguments.  The asm version pushes the args
 *          right-to-left; in C, the compiler does that for us.
 */

#include <stdio.h>
#include <stdlib.h>

static const char *name = "Jonathan";
static const char *personstring = "person";
static int numberloved = 3;

int main(void) {
    printf("Hello! %s is a %s who loves the number %d\n", name, personstring,
           numberloved);
    exit(0);
}
