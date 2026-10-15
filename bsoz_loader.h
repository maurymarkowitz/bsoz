#ifndef BSOZ_LOADER_H
#define BSOZ_LOADER_H

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdbool.h>

/*
 * File Loader Header
 *
 * This header provides functionality to load binary files into emulated
 * memory at specified addresses from the command line. Supports both
 * positional arguments (FILE@ADDRESS) and option syntax (--load FILE@ADDRESS).
 */

/* Forward declaration of struct cpu from bsoz.h */
struct cpu;

struct file_load {
  char *path;        /* path to file */
  uint16_t address;  /* load address */
};

struct load_config {
  struct file_load *files;      /* array of files to load */
  size_t file_count;            /* number of files */
  uint16_t start_addr;          /* start execution address */
  bool use_reset_vector;        /* if false, use start_addr */
  bool auto_run;                /* if true, start execution immediately */
};

/**
 * Initialize load configuration structure
 *
 * Args:
 *   config: pointer to load_config structure
 */
void load_config_init(struct load_config *config) {
  if (!config) {
    return;
  }
  config->files = NULL;
  config->file_count = 0;
  config->start_addr = 0;
  config->use_reset_vector = true;
  config->auto_run = true;
}

/**
 * Add a file to the load configuration
 *
 * Args:
 *   config: pointer to load_config structure
 *   path: path to the file
 *   address: load address (0-65535)
 *
 * Returns:
 *   0 on success, -1 on error
 */
int load_config_add_file(struct load_config *config, const char *path,
                         uint16_t address) {
  if (!config || !path) {
    return -1;
  }

  struct file_load *new_files =
      realloc(config->files, (config->file_count + 1) * sizeof(struct file_load));
  if (!new_files) {
    fprintf(stderr, "Error: memory allocation failed\n");
    return -1;
  }

  config->files = new_files;
  config->files[config->file_count].path = malloc(strlen(path) + 1);
  if (!config->files[config->file_count].path) {
    fprintf(stderr, "Error: memory allocation failed\n");
    return -1;
  }

  strcpy(config->files[config->file_count].path, path);
  config->files[config->file_count].address = address;
  config->file_count++;

  return 0;
}

/**
 * Free resources allocated by load configuration
 *
 * Args:
 *   config: pointer to load_config structure
 */
void load_config_free(struct load_config *config) {
  if (!config) {
    return;
  }

  for (size_t i = 0; i < config->file_count; i++) {
    if (config->files[i].path) {
      free(config->files[i].path);
    }
  }

  if (config->files) {
    free(config->files);
  }

  config->files = NULL;
  config->file_count = 0;
}

/**
 * Parse a file specification string
 *
 * Format: "filename@address" or "filename" (returns -1 for address)
 *
 * Args:
 *   spec: specification string
 *   path: output buffer for filename
 *   path_len: size of path buffer
 *   address: output for address (-1 if not specified)
 *
 * Returns:
 *   0 on success, -1 on error
 */
int parse_file_spec(const char *spec, char *path, size_t path_len,
                    uint16_t *address) {
  if (!spec || !path || path_len == 0) {
    return -1;
  }

  const char *at = strchr(spec, '@');

  if (at) {
    /* Found @ separator, parse address */
    size_t filename_len = at - spec;

    if (filename_len >= path_len) {
      fprintf(stderr, "Error: filename too long\n");
      return -1;
    }

    strncpy(path, spec, filename_len);
    path[filename_len] = '\0';

    char *endptr;
    long addr = strtol(at + 1, &endptr, 10);

    if (*endptr != '\0' || addr < 0 || addr > 65535) {
      fprintf(stderr, "Error: invalid address '%s' (expected decimal 0-65535)\n",
              at + 1);
      return -1;
    }

    *address = (uint16_t)addr;
    return 0;
  } else {
    /* No @ separator, filename only */
    if (strlen(spec) >= path_len) {
      fprintf(stderr, "Error: filename too long\n");
      return -1;
    }

    strcpy(path, spec);
    *address = 0xFFFF; /* sentinel value meaning not specified */
    return 0;
  }
}

/**
 * Parse command-line arguments for file loading
 *
 * Supports:
 *   - Positional: program.bin@256
 *   - Option: --load program.bin@256
 *   - Option: --load program.bin 256
 *   - Short: -l program.bin@256
 *   - Mix: program.bin@256 --load data.bin@512
 *
 * Args:
 *   argc: argument count
 *   argv: argument vector
 *   config: output configuration structure
 *   exclude_mask: bitmask of which arguments were processed (output)
 *
 * Returns:
 *   0 on success, -1 on parse error
 */
int parse_load_args(int argc, char *argv[], struct load_config *config,
                    bool *exclude_mask) {
  if (!config || !argv) {
    return -1;
  }

  load_config_init(config);

  if (exclude_mask) {
    for (int i = 0; i < argc; i++) {
      exclude_mask[i] = false;
    }
  }

  for (int i = 1; i < argc; i++) {
    if (exclude_mask) {
      exclude_mask[i] = false;
    }

    const char *arg = argv[i];

    /* Check for --load or -l option */
    if (strcmp(arg, "--load") == 0 || strcmp(arg, "-l") == 0) {
      if (exclude_mask) {
        exclude_mask[i] = true;
      }

      if (i + 1 >= argc) {
        fprintf(stderr, "Error: %s requires a filename\n", arg);
        return -1;
      }

      const char *filespec = argv[i + 1];
      i++;

      if (exclude_mask) {
        exclude_mask[i] = true;
      }

      char path[512];
      uint16_t address;

      if (parse_file_spec(filespec, path, sizeof(path), &address) < 0) {
        return -1;
      }

      /* If address not in filespec, try next argument */
      if (address == 0xFFFF) {
        if (i + 1 >= argc) {
          fprintf(stderr,
                  "Error: --load filename '%s' missing address\n", filespec);
          return -1;
        }

        const char *addr_str = argv[i + 1];

        /* Check if it looks like an option, not an address */
        if (addr_str[0] == '-') {
          fprintf(stderr,
                  "Error: --load filename '%s' missing address\n", filespec);
          return -1;
        }

        i++;

        if (exclude_mask) {
          exclude_mask[i] = true;
        }

        char *endptr;
        long addr = strtol(addr_str, &endptr, 10);

        if (*endptr != '\0' || addr < 0 || addr > 65535) {
          fprintf(stderr,
                  "Error: invalid address '%s' (expected decimal 0-65535)\n",
                  addr_str);
          return -1;
        }

        address = (uint16_t)addr;
      }

      if (load_config_add_file(config, path, address) < 0) {
        return -1;
      }
    }
    /* Check for --start or -s option */
    else if (strcmp(arg, "--start") == 0 || strcmp(arg, "-s") == 0) {
      if (exclude_mask) {
        exclude_mask[i] = true;
      }

      if (i + 1 >= argc) {
        fprintf(stderr, "Error: %s requires an address\n", arg);
        return -1;
      }

      i++;

      if (exclude_mask) {
        exclude_mask[i] = true;
      }

      char *endptr;
      long addr = strtol(argv[i], &endptr, 10);

      if (*endptr != '\0' || addr < 0 || addr > 65535) {
        fprintf(stderr,
                "Error: invalid start address '%s' (expected decimal 0-65535)\n",
                argv[i]);
        return -1;
      }

      config->start_addr = (uint16_t)addr;
      config->use_reset_vector = false;
    }
    /* Check for --no-auto-run or -n option */
    else if (strcmp(arg, "--no-auto-run") == 0 || strcmp(arg, "-n") == 0) {
      if (exclude_mask) {
        exclude_mask[i] = true;
      }

      config->auto_run = false;
    }
    /* Check for positional argument (FILE@ADDRESS) */
    else if (arg[0] != '-') {
      char path[512];
      uint16_t address;

      if (parse_file_spec(arg, path, sizeof(path), &address) < 0) {
        return -1;
      }

      if (address == 0xFFFF) {
        fprintf(stderr,
                "Error: invalid file specification '%s' (expected 'file@address')\n",
                arg);
        return -1;
      }

      if (load_config_add_file(config, path, address) < 0) {
        return -1;
      }

      if (exclude_mask) {
        exclude_mask[i] = true;
      }
    }
  }

  return 0;
}

/**
 * Load all configured files into memory
 *
 * Args:
 *   mem: memory array (must be at least 65536 bytes)
 *   config: load configuration
 *
 * Returns:
 *   0 on success, -1 on file I/O error
 */
int load_files(uint8_t *mem, const struct load_config *config) {
  if (!mem || !config) {
    return -1;
  }

  for (size_t i = 0; i < config->file_count; i++) {
    const char *path = config->files[i].path;
    uint16_t address = config->files[i].address;

    FILE *f = fopen(path, "rb");
    if (!f) {
      perror(path);
      return -1;
    }

    /* Get file size */
    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (file_size < 0) {
      perror(path);
      fclose(f);
      return -1;
    }

    /* Check if file fits in memory */
    uint32_t available = 65536 - address;
    if ((uint32_t)file_size > available) {
      fprintf(
          stderr,
          "Error: file '%s' (%ld bytes) exceeds available space at address "
          "%u (%u bytes)\n",
          path, file_size, address, available);
      fclose(f);
      return -1;
    }

    /* Read file into memory */
    size_t bytes_read = fread(&mem[address], 1, file_size, f);
    if ((long)bytes_read != file_size) {
      fprintf(stderr, "Error reading file '%s': incomplete read\n", path);
      fclose(f);
      return -1;
    }

    fclose(f);
  }

  return 0;
}

/**
 * Get startup address based on configuration
 *
 * Args:
 *   c: pointer to CPU structure (only used if reading reset vector)
 *   config: load configuration
 *
 * Returns:
 *   Address to start execution from
 */
uint16_t get_startup_addr(struct cpu *c, const struct load_config *config) {
  if (!config) {
    return 0xFFFC; /* Default to reset vector */
  }

  if (!config->use_reset_vector) {
    return config->start_addr;
  }

  /* Default: use reset vector (requires reading memory) */
  if (c) {
    /* Will be read from 0xFFFC-0xFFFD by rst() function */
    return 0xFFFC;
  }

  return 0xFFFC;
}

#endif /* BSOZ_LOADER_H */
