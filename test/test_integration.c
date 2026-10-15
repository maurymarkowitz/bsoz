/*
 * Integration test for file loading feature
 *
 * This test demonstrates that files can be loaded at specified
 * addresses and then executed by the emulators.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "../bsoz.h"
#include "../bsoz_loader.h"

uint8_t mem[0x10000];

uint8_t r8(struct cpu *c, uint16_t addr) {
  (void)c;
  return mem[addr];
}

uint8_t w8(struct cpu *c, uint16_t addr, uint8_t b) {
  (void)c;
  return mem[addr] = b;
}

/*
 * Test loading a file with an @ separator
 */
int test_load_file_at_address(void) {
  struct load_config config;
  load_config_init(&config);

  /* Create a temporary test file */
  FILE *f = fopen("/tmp/test_load.bin", "wb");
  if (!f) {
    printf("FAIL: Could not create test file\n");
    return 1;
  }

  uint8_t test_data[] = {0xEA, 0xEA, 0x00};
  fwrite(test_data, sizeof(test_data), 1, f);
  fclose(f);

  if (load_config_add_file(&config, "/tmp/test_load.bin", 0x1000) != 0) {
    printf("FAIL: Could not add file to config\n");
    return 1;
  }

  if (load_files(mem, &config) != 0) {
    printf("FAIL: Could not load file\n");
    load_config_free(&config);
    return 1;
  }

  if (mem[0x1000] != 0xEA || mem[0x1001] != 0xEA || mem[0x1002] != 0x00) {
    printf("FAIL: File contents not loaded correctly\n");
    printf("  Expected: EA EA 00\n");
    printf("  Got: %02x %02x %02x\n", mem[0x1000], mem[0x1001], mem[0x1002]);
    load_config_free(&config);
    return 1;
  }

  load_config_free(&config);
  printf("PASS: test_load_file_at_address\n");
  return 0;
}

/*
 * Test that CPU can execute loaded code
 */
int test_execute_loaded_code(void) {
  struct load_config config;
  struct cpu cpu = {0};
  memset(mem, 0, sizeof(mem));
  load_config_init(&config);

  /* Create a temporary test file with some NOPs and a BRK */
  FILE *f = fopen("/tmp/test_exec.bin", "wb");
  if (!f) {
    printf("FAIL: Could not create test file\n");
    return 1;
  }

  uint8_t test_data[] = {0xEA, 0xEA, 0x00};  /* NOP NOP BRK */
  fwrite(test_data, sizeof(test_data), 1, f);
  fclose(f);

  if (load_config_add_file(&config, "/tmp/test_exec.bin", 0x0400) != 0) {
    printf("FAIL: Could not add file to config\n");
    return 1;
  }

  if (load_files(mem, &config) != 0) {
    printf("FAIL: Could not load file\n");
    load_config_free(&config);
    return 1;
  }

  /* Manually set PC to start address, bypassing reset vector */
  cpu.pc = 0x0400;

  /* Execute 3 instructions (2 NOPs and 1 BRK) */
  for (int i = 0; i < 3; i++) {
    int result = step(&cpu);
    if (result < 0 && i < 2) {
      printf("FAIL: Unexpected error during execution\n");
      load_config_free(&config);
      return 1;
    }
  }

  uint16_t expected_pc = 0x0400 + 2;  /* 2 NOPs should advance PC by 2 */
  if (cpu.pc != expected_pc && cpu.pc != 0x0000) {
    printf("FAIL: PC not advanced correctly after 2 NOPs\n");
    printf("  Expected: 0x%04x or 0x0000 (after BRK)\n", expected_pc);
    printf("  Got: 0x%04x\n", cpu.pc);
    load_config_free(&config);
    return 1;
  }

  load_config_free(&config);
  printf("PASS: test_execute_loaded_code\n");
  return 0;
}

/*
 * Test loading multiple files
 */
int test_load_multiple_files(void) {
  struct load_config config;
  memset(mem, 0, sizeof(mem));
  load_config_init(&config);

  /* Create first test file */
  FILE *f1 = fopen("/tmp/test_multi1.bin", "wb");
  if (!f1) {
    printf("FAIL: Could not create first test file\n");
    return 1;
  }
  uint8_t data1[] = {0x11, 0x22, 0x33};
  fwrite(data1, sizeof(data1), 1, f1);
  fclose(f1);

  /* Create second test file */
  FILE *f2 = fopen("/tmp/test_multi2.bin", "wb");
  if (!f2) {
    printf("FAIL: Could not create second test file\n");
    return 1;
  }
  uint8_t data2[] = {0x44, 0x55, 0x66};
  fwrite(data2, sizeof(data2), 1, f2);
  fclose(f2);

  if (load_config_add_file(&config, "/tmp/test_multi1.bin", 0x1000) != 0) {
    printf("FAIL: Could not add first file\n");
    return 1;
  }

  if (load_config_add_file(&config, "/tmp/test_multi2.bin", 0x2000) != 0) {
    printf("FAIL: Could not add second file\n");
    return 1;
  }

  if (load_files(mem, &config) != 0) {
    printf("FAIL: Could not load files\n");
    load_config_free(&config);
    return 1;
  }

  if (mem[0x1000] != 0x11 || mem[0x1001] != 0x22 || mem[0x1002] != 0x33) {
    printf("FAIL: First file not loaded correctly\n");
    load_config_free(&config);
    return 1;
  }

  if (mem[0x2000] != 0x44 || mem[0x2001] != 0x55 || mem[0x2002] != 0x66) {
    printf("FAIL: Second file not loaded correctly\n");
    load_config_free(&config);
    return 1;
  }

  load_config_free(&config);
  printf("PASS: test_load_multiple_files\n");
  return 0;
}

int main(void) {
  int failed = 0;

  printf("Running integration tests...\n\n");

  failed += test_load_file_at_address();
  failed += test_execute_loaded_code();
  failed += test_load_multiple_files();

  printf("\n");
  if (failed == 0) {
    printf("All integration tests passed!\n");
    return 0;
  } else {
    printf("%d test(s) failed\n", failed);
    return 1;
  }
}
