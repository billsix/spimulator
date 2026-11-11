/* Check that the generated decode table gives every encoding the token the
   C engine's old decoder found.

   The old instruction.c copied every row into a_opcode_tbl as {name,
   encoding, token}, sorted it with qsort on the encoding at startup, and
   decoded with bsearch.  This program does exactly that over the generated
   opcode_encoding_tbl (the same rows), then compares the answer for every
   distinct encoding with opcode_decode_tbl.

   Build and run from the repo root:
     cc -std=gnu23 -Iinclude -o /tmp/check_decode \
        tasks/adhoc/opcodes-x-macro-redesign/check_decode.c src/opcode-tables.c
     /tmp/check_decode */

#include <stdio.h>
#include <stdlib.h>

#include "opcode-tables.h"

static int compare_pair_value(const void* a, const void* b) {
  const name_val_val* p1 = a;
  const name_val_val* p2 = b;
  return (p1->value1 > p2->value1) - (p1->value1 < p2->value1);
}

static int cmp_key_to_value1(const void* key, const void* elem) {
  const int k = *(const int*)key;
  const int v = ((const name_val_val*)elem)->value1;
  return (k > v) - (k < v);
}

int main(void) {
  static name_val_val old[OPCODE_COUNT];
  for (int i = 0; i < OPCODE_COUNT; i++)
    old[i] = (name_val_val){opcode_encoding_tbl[i].name,
                            opcode_encoding_tbl[i].value2,
                            opcode_encoding_tbl[i].value1};
  qsort(old, OPCODE_COUNT, sizeof old[0], compare_pair_value);
  int mismatches = 0;
  int distinct = 0;
  for (int i = 0; i < OPCODE_COUNT; i++) {
    if (i > 0 && old[i].value1 == old[i - 1].value1) continue;
    distinct++;
    const int key = old[i].value1;
    const name_val_val* hit =
        bsearch(&key, old, OPCODE_COUNT, sizeof old[0], cmp_key_to_value1);
    const name_val_val* now =
        bsearch(&key, opcode_decode_tbl, OPCODE_DECODE_COUNT,
                sizeof opcode_decode_tbl[0], cmp_key_to_value1);
    if (now == nullptr || hit->value2 != now->value2) {
      printf("MISMATCH %#x: old %s, generated %s\n", (unsigned)key, hit->name,
             now ? now->name : "(none)");
      mismatches++;
    }
  }
  printf("%d encodings, %d mismatches\n", distinct, mismatches);
  return mismatches != 0 || distinct != OPCODE_DECODE_COUNT;
}
