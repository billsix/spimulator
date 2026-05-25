/* Record definition shared by the records-chapter programs.
 *
 * On disk each record is a flat 324-byte block laid out exactly
 * like the .equ offsets in record-def.s:
 *
 *   firstname  offset   0,  40 bytes
 *   lastname   offset  40,  40 bytes
 *   address    offset  80, 240 bytes
 *   age        offset 320,   4 bytes (int)
 *
 * Default i386 alignment (4) gives this struct size 324 with
 * no internal padding, matching the original layout.
 */

#ifndef RECORD_DEF_H
#define RECORD_DEF_H

struct record {
    char firstname[40];
    char lastname[40];
    char address[240];
    int age;
};

#define RECORD_SIZE 324

#endif
