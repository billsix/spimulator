/* PURPOSE: Read an input file, convert lower-case ASCII to upper
 *          case, and write the result to an output file.
 *
 * USAGE:   ./toupper-nomm-simplified <input> <output>
 *
 * Multi-arch crt0 shim: the kernel hands _start the argv vector
 * directly on the stack with no preceding return address, which
 * the C calling convention can't express.  We provide a tiny
 * per-arch _start written in inline assembly that pulls argc and
 * argv off the stack, calls a regular C my_main(argc, argv), and
 * exits with my_main's return value.  This is the same trick a
 * minimal libc's crt0 does, just inline here so we stay -nostdlib.
 */

#include "os.h"

#define BUFFER_SIZE 500

#define LOWERCASE_A 'a'
#define LOWERCASE_Z 'z'
#define UPPER_CONVERSION ('A' - 'a')

#if defined(__i386__)
__asm__(
    ".global _start\n"
    "_start:\n"
    "    movl  (%esp), %eax\n"  /* argc                  */
    "    leal  4(%esp), %edx\n" /* argv                  */
    "    pushl %edx\n"
    "    pushl %eax\n"
    "    call  my_main\n"
    "    movl  %eax, %ebx\n" /* return value -> exit  */
    "    movl  $1, %eax\n"   /* SYS_EXIT              */
    "    int   $0x80\n");
#elif defined(__x86_64__)
__asm__(
    ".global _start\n"
    "_start:\n"
    "    movq  (%rsp), %rdi\n"  /* argc                  */
    "    leaq  8(%rsp), %rsi\n" /* argv                  */
    "    call  my_main\n"
    "    movl  %eax, %edi\n"
    "    movl  $60, %eax\n" /* SYS_EXIT              */
    "    syscall\n");
#elif defined(__arm__)
__asm__(
    ".global _start\n"
    "_start:\n"
    "    ldr   r0, [sp]\n"   /* argc                  */
    "    add   r1, sp, #4\n" /* argv                  */
    "    bl    my_main\n"
    "    mov   r7, #1\n" /* SYS_EXIT              */
    "    svc   #0\n");
#elif defined(__aarch64__)
__asm__(
    ".global _start\n"
    "_start:\n"
    "    ldr   w0, [sp]\n"   /* argc                  */
    "    add   x1, sp, #8\n" /* argv                  */
    "    bl    my_main\n"
    "    mov   x8, #93\n" /* SYS_EXIT              */
    "    svc   #0\n");
#elif defined(__mips__)
__asm__(
    ".global _start\n"
    "_start:\n"
    "    lw    $4, 0($29)\n" /* a0 = argc             */
    "    addiu $5, $29, 4\n" /* a1 = argv             */
    "    jal   my_main\n"
    "    nop\n"            /* delay slot            */
    "    move  $4, $2\n"   /* status = my_main ret  */
    "    li    $2, 4001\n" /* SYS_EXIT              */
    "    syscall\n");
#else
#error "Add a _start crt0 shim for this architecture"
#endif

static char BUFFER_DATA[BUFFER_SIZE];

static void convert_to_upper(char *buf, int len) {
    for (int i = 0; i < len; i++) {
        if (buf[i] >= LOWERCASE_A && buf[i] <= LOWERCASE_Z) {
            buf[i] += UPPER_CONVERSION;
        }
    }
}

int my_main(int argc, char **argv) {
    (void)argc;

    int fd_in = (int)os_open(argv[1], OS_O_RDONLY, 0666);
    int fd_out =
        (int)os_open(argv[2], OS_O_WRONLY | OS_O_CREAT | OS_O_TRUNC, 0666);

    while (1) {
        int n = (int)os_read(fd_in, BUFFER_DATA, BUFFER_SIZE);
        if (n <= END_OF_FILE) {
            break;
        }

        convert_to_upper(BUFFER_DATA, n);

        os_write(fd_out, BUFFER_DATA, n);
    }

    os_close(fd_out);
    os_close(fd_in);

    return 0;
}
