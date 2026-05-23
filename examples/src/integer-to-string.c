/* PURPOSE: Convert an integer to a decimal string for display.
 *
 * INPUT:   value  - the integer to convert
 *          buffer - a buffer large enough to hold the result
 *                   (11 bytes is enough for any 32-bit int + NUL)
 *
 * OUTPUT:  The buffer is filled with the decimal representation
 *          followed by a null terminator.
 *
 * The original asm pushes each digit onto the stack so it can be
 * popped back off in reverse.  We do the equivalent here with a
 * small local array.  Same algorithm: divide by 10, take the
 * remainder, repeat until the quotient is zero, then emit the
 * digits in reverse.
 */

void integer2string(int value, char* buffer) {
  char digits[16];
  int count = 0;

  do {
    digits[count] = (value % 10) + '0';
    count++;
    value = value / 10;
  } while (value != 0);

  while (count > 0) {
    count--;
    *buffer = digits[count];
    buffer++;
  }
  *buffer = '\0';
}
