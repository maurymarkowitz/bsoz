#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "../bsoz_stats.h"

/**
 * Test suite for bsoz_stats functionality
 */

int test_cpu_stats_init(void) {
  struct cpu_stats stats;
  cpu_stats_init(&stats);

  for (int i = 0; i < 256; i++) {
    if (stats.opcodes[i].count != 0 || stats.opcodes[i].cycles != 0) {
      printf("FAIL: cpu_stats_init - not zeroed\n");
      return 1;
    }
  }

  if (stats.total_cycles != 0) {
    printf("FAIL: cpu_stats_init - total_cycles not zero\n");
    return 1;
  }

  printf("PASS: cpu_stats_init\n");
  return 0;
}

int test_cpu_stats_update(void) {
  struct cpu_stats stats;
  cpu_stats_init(&stats);

  cpu_stats_update(&stats, 0xEA, 2);

  if (stats.opcodes[0xEA].count != 1) {
    printf("FAIL: cpu_stats_update - count not incremented\n");
    return 1;
  }

  if (stats.opcodes[0xEA].cycles != 2) {
    printf("FAIL: cpu_stats_update - cycles not set\n");
    return 1;
  }

  if (stats.total_cycles != 2) {
    printf("FAIL: cpu_stats_update - total_cycles not set\n");
    return 1;
  }

  printf("PASS: cpu_stats_update\n");
  return 0;
}

int test_cpu_stats_multiple_updates(void) {
  struct cpu_stats stats;
  cpu_stats_init(&stats);

  cpu_stats_update(&stats, 0xEA, 2);
  cpu_stats_update(&stats, 0xEA, 2);
  cpu_stats_update(&stats, 0xA9, 2);

  if (stats.opcodes[0xEA].count != 2) {
    printf("FAIL: cpu_stats_multiple_updates - EA count incorrect\n");
    return 1;
  }

  if (stats.opcodes[0xA9].count != 1) {
    printf("FAIL: cpu_stats_multiple_updates - A9 count incorrect\n");
    return 1;
  }

  if (stats.total_cycles != 6) {
    printf("FAIL: cpu_stats_multiple_updates - total_cycles incorrect\n");
    return 1;
  }

  printf("PASS: cpu_stats_multiple_updates\n");
  return 0;
}

int test_get_opcode_cycles(void) {
  struct cpu_stats stats;
  cpu_stats_init(&stats);

  uint16_t cycles;

  cycles = get_opcode_cycles(0xEA);
  if (cycles != 2) {
    printf("FAIL: get_opcode_cycles - NOP should be 2 cycles\n");
    return 1;
  }

  cycles = get_opcode_cycles(0xA9);
  if (cycles != 2) {
    printf("FAIL: get_opcode_cycles - LDA imm should be 2 cycles\n");
    return 1;
  }

  cycles = get_opcode_cycles(0xAD);
  if (cycles != 4) {
    printf("FAIL: get_opcode_cycles - LDA abs should be 4 cycles\n");
    return 1;
  }

  printf("PASS: get_opcode_cycles\n");
  return 0;
}

int test_cpu_stats_reset(void) {
  struct cpu_stats stats;
  cpu_stats_init(&stats);

  cpu_stats_update(&stats, 0xEA, 2);
  cpu_stats_update(&stats, 0xA9, 2);

  if (stats.total_cycles != 4) {
    printf("FAIL: cpu_stats_reset - pre-reset total_cycles not set\n");
    return 1;
  }

  cpu_stats_reset(&stats);

  if (stats.total_cycles != 0) {
    printf("FAIL: cpu_stats_reset - total_cycles not zero\n");
    return 1;
  }

  for (int i = 0; i < 256; i++) {
    if (stats.opcodes[i].count != 0 || stats.opcodes[i].cycles != 0) {
      printf("FAIL: cpu_stats_reset - opcodes not zeroed\n");
      return 1;
    }
  }

  printf("PASS: cpu_stats_reset\n");
  return 0;
}

int test_cpu_stats_report(void) {
  struct cpu_stats stats;
  cpu_stats_init(&stats);

  for (int i = 0; i < 100; i++) {
    cpu_stats_update(&stats, 0xEA, 2);
  }

  for (int i = 0; i < 50; i++) {
    cpu_stats_update(&stats, 0xA9, 2);
  }

  printf("\n");
  cpu_stats_report(&stats, stdout);

  printf("PASS: cpu_stats_report\n");
  return 0;
}

int main(void) {
  int failed = 0;

  printf("Running bsoz_stats tests...\n\n");

  failed += test_cpu_stats_init();
  failed += test_cpu_stats_update();
  failed += test_cpu_stats_multiple_updates();
  failed += test_get_opcode_cycles();
  failed += test_cpu_stats_reset();
  failed += test_cpu_stats_report();

  printf("\n");
  if (failed == 0) {
    printf("All tests passed!\n");
    return 0;
  } else {
    printf("%d test(s) failed\n", failed);
    return 1;
  }
}
