#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "../bsoz_loader.h"

/**
 * Test suite for bsoz_loader functionality
 */

int test_parse_file_spec_with_address(void) {
  char path[512];
  uint16_t address;

  if (parse_file_spec("program.bin@256", path, sizeof(path), &address) != 0) {
    return 1;
  }

  if (strcmp(path, "program.bin") != 0 || address != 256) {
    printf("FAIL: parse_file_spec with address\n");
    return 1;
  }

  printf("PASS: parse_file_spec with address\n");
  return 0;
}

int test_parse_file_spec_without_address(void) {
  char path[512];
  uint16_t address;

  if (parse_file_spec("program.bin", path, sizeof(path), &address) != 0) {
    return 1;
  }

  if (strcmp(path, "program.bin") != 0 || address != 0xFFFF) {
    printf("FAIL: parse_file_spec without address\n");
    return 1;
  }

  printf("PASS: parse_file_spec without address\n");
  return 0;
}

int test_load_config_add_file(void) {
  struct load_config config;
  load_config_init(&config);

  if (load_config_add_file(&config, "program.bin", 256) != 0) {
    printf("FAIL: load_config_add_file\n");
    return 1;
  }

  if (config.file_count != 1 || config.files[0].address != 256 ||
      strcmp(config.files[0].path, "program.bin") != 0) {
    printf("FAIL: load_config_add_file - incorrect data\n");
    return 1;
  }

  load_config_free(&config);
  printf("PASS: load_config_add_file\n");
  return 0;
}

int test_load_config_multiple_files(void) {
  struct load_config config;
  load_config_init(&config);

  load_config_add_file(&config, "program.bin", 256);
  load_config_add_file(&config, "data.bin", 512);
  load_config_add_file(&config, "lib.bin", 1024);

  if (config.file_count != 3) {
    printf("FAIL: load_config_multiple_files - count mismatch\n");
    return 1;
  }

  if (config.files[0].address != 256 || config.files[1].address != 512 ||
      config.files[2].address != 1024) {
    printf("FAIL: load_config_multiple_files - addresses mismatch\n");
    return 1;
  }

  load_config_free(&config);
  printf("PASS: load_config_multiple_files\n");
  return 0;
}

int test_parse_load_args_positional(void) {
  struct load_config config;
  bool exclude[10] = {false};

  const char *argv[] = {"program", "test.bin@256", "data.bin@512"};
  int argc = 3;

  if (parse_load_args(argc, (char **)argv, &config, exclude) != 0) {
    printf("FAIL: parse_load_args_positional - parse failed\n");
    return 1;
  }

  if (config.file_count != 2 || config.files[0].address != 256 ||
      config.files[1].address != 512) {
    printf("FAIL: parse_load_args_positional - incorrect data\n");
    return 1;
  }

  load_config_free(&config);
  printf("PASS: parse_load_args_positional\n");
  return 0;
}

int test_parse_load_args_option_with_at(void) {
  struct load_config config;
  bool exclude[10] = {false};

  const char *argv[] = {"program", "--load", "test.bin@256"};
  int argc = 3;

  if (parse_load_args(argc, (char **)argv, &config, exclude) != 0) {
    printf("FAIL: parse_load_args_option_with_at - parse failed\n");
    return 1;
  }

  if (config.file_count != 1 || config.files[0].address != 256 ||
      strcmp(config.files[0].path, "test.bin") != 0) {
    printf("FAIL: parse_load_args_option_with_at - incorrect data\n");
    return 1;
  }

  load_config_free(&config);
  printf("PASS: parse_load_args_option_with_at\n");
  return 0;
}

int test_parse_load_args_option_separate_address(void) {
  struct load_config config;
  bool exclude[10] = {false};

  const char *argv[] = {"program", "--load", "test.bin", "256"};
  int argc = 4;

  if (parse_load_args(argc, (char **)argv, &config, exclude) != 0) {
    printf("FAIL: parse_load_args_option_separate - parse failed\n");
    return 1;
  }

  if (config.file_count != 1 || config.files[0].address != 256 ||
      strcmp(config.files[0].path, "test.bin") != 0) {
    printf("FAIL: parse_load_args_option_separate - incorrect data\n");
    return 1;
  }

  load_config_free(&config);
  printf("PASS: parse_load_args_option_separate_address\n");
  return 0;
}

int test_parse_load_args_with_start(void) {
  struct load_config config;
  bool exclude[10] = {false};

  const char *argv[] = {"program", "test.bin@256", "--start", "256"};
  int argc = 4;

  if (parse_load_args(argc, (char **)argv, &config, exclude) != 0) {
    printf("FAIL: parse_load_args_with_start - parse failed\n");
    return 1;
  }

  if (config.start_addr != 256 || !config.use_reset_vector == false) {
    printf("FAIL: parse_load_args_with_start - start address not set\n");
    return 1;
  }

  load_config_free(&config);
  printf("PASS: parse_load_args_with_start\n");
  return 0;
}

int main(void) {
  int failed = 0;

  printf("Running bsoz_loader tests...\n\n");

  failed += test_parse_file_spec_with_address();
  failed += test_parse_file_spec_without_address();
  failed += test_load_config_add_file();
  failed += test_load_config_multiple_files();
  failed += test_parse_load_args_positional();
  failed += test_parse_load_args_option_with_at();
  failed += test_parse_load_args_option_separate_address();
  failed += test_parse_load_args_with_start();

  printf("\n");
  if (failed == 0) {
    printf("All tests passed!\n");
    return 0;
  } else {
    printf("%d test(s) failed\n", failed);
    return 1;
  }
}
