/* PURPOSE:  Count the characters until a null byte is reached.
 *
 * INPUT:    The address of a null-terminated string
 *
 * OUTPUT:   Returns the count
 *
 * This is a library file — it has no _start.  It is linked
 * into other programs (conversion-program, read-records,
 * error-exit, ...).
 */

int count_chars(const char *s) {
    int count = 0;
    while (*s != '\0') {
        count++;
        s++;
    }
    return count;
}
