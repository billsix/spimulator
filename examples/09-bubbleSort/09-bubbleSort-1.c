#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#define ARSIZE 10

int main(int argc, char *argv[]) {
  char ch_arr[ARSIZE];

  size_t lastchar = 0;
  int32_t stop = 0;
  /*
   * Read characters into array.
   * Stop if end of line, or array full.
   */
  while (stop != 1) {
    ch_arr[lastchar] = getchar();
    if (ch_arr[lastchar] == '\n')
      stop = 1;
    else
      lastchar = lastchar + 1;
    if (lastchar == ARSIZE)
      stop = 1;
  }
  lastchar = lastchar - 1;

  /*
   * Now the traditional bubble sort.
   */
  int32_t count1 = 0;
  while (count1 < lastchar) {
    int32_t count2 = count1 + 1;
    while (count2 <= lastchar) {
      if (ch_arr[count1] > ch_arr[count2]) {
        /* swap */
        int32_t temp;
        temp = ch_arr[count1];
        ch_arr[count1] = ch_arr[count2];
        ch_arr[count2] = temp;
      }
      count2 = count2 + 1;
    }
    count1 = count1 + 1;
  }

  count1 = 0;
  while (count1 <= lastchar) {
    printf("%c\n", ch_arr[count1]);
    count1 = count1 + 1;
  }
  exit(EXIT_SUCCESS);
}
