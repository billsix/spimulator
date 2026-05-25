/* PURPOSE: Simple program that exits and returns a status code
 *          back to the Linux kernel.
 *
 * INPUT:   none
 *
 * OUTPUT:  returns a status code, viewable with `echo $?`.
 *
 * The arch-specific syscall machinery lives in os.h — this
 * program builds for i386, x86_64, arm, aarch64, and mips Linux
 * with no source-level changes.
 */

#include "os.h"

__attribute__((noreturn)) void _start(void) { os_exit(0); }
