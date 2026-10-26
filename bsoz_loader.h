/*
 * File Loader Header
 *
 * This header provides functionality to load binary files into emulated
 * memory at specified addresses from the command line. Supports multiple
 * formats:
 *   - RAW binary (explicit address required)
 *   - PRG (Commodore format: 2-byte address header + data)
 *   - HEX (Intel HEX format: ASCII text with address/checksum records)
 */

#ifndef BSOZ_LOADER_H
#define BSOZ_LOADER_H

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdbool.h>
#include <ctype.h>

/* Forward declaration of struct cpu from bsoz.h */
struct cpu;

/* File format enumeration */
typedef enum {
  FORMAT_AUTO,   /* Detect format from extension */
  FORMAT_RAW,    /* Raw binary data */
  FORMAT_PRG,    /* Commodore PRG format */
  FORMAT_HEX     /* Intel HEX format */
} file_format_t;

struct file_load {
  char *path;        /* path to file */
  uint16_t address;  /* load address (used for RAW format) */
  file_format_t fmt; /* file format */
};

struct load_config {
  struct file_load *files;      /* array of files to load */
  size_t file_count;            /* number of files */
  uint16_t start_addr;          /* start execution address */
  bool use_reset_vector;        /* if false, use start_addr */
  bool auto_run;                /* if true, start execution immediately */
};

/**
 * Detect file format from extension or magic bytes
 *
 * Args:
 *   path: file path
 *   fmt: format hint (FORMAT_AUTO to detect)
 *
 * Returns:
 *   Detected format (FORMAT_RAW if detection fails)
 */
static file_format_t detect_format(const char *path, file_format_t fmt) {
  if (fmt != FORMAT_AUTO) {
    return fmt;
  }

  /* Check file extension */
  const char *dot = strrchr(path, '.');
  if (dot) {
    if (strcmp(dot, ".prg") == 0) return FORMAT_PRG;
    if (strcmp(dot, ".PRG") == 0) return FORMAT_PRG;
    if (strcmp(dot, ".hex") == 0) return FORMAT_HEX;
    if (strcmp(dot, ".HEX") == 0) return FORMAT_HEX;
  }

  return FORMAT_RAW;
}

/**
 * Load PRG file (Commodore format: 2-byte LE address + data)
 *
 * Args:
 *   f: open file handle
 *   mem: memory array
 *   path: file path (for error messages)
 *
 * Returns:
 *   0 on success, -1 on error
 */
static int load_prg_file(FILE *f, uint8_t *mem, const char *path) {
  /* Read 2-byte address header (little-endian) */
  uint8_t addr_bytes[2];
  if (fread(addr_bytes, 1, 2, f) != 2) {
    fprintf(stderr, "Error: PRG file '%s' too short (need 2-byte address)\n",
            path);
    return -1;
  }

  uint16_t load_addr = addr_bytes[0] | (addr_bytes[1] << 8);

  /* Read remaining file data */
  fseek(f, 0, SEEK_END);
  long file_size = ftell(f);
  fseek(f, 2, SEEK_SET); /* Back to after address header */

  long data_size = file_size - 2;
  if (data_size < 0) {
    fprintf(stderr, "Error: PRG file '%s' too short\n", path);
    return -1;
  }

  uint32_t available = 65536 - load_addr;
  if ((uint32_t)data_size > available) {
    fprintf(stderr,
            "Error: PRG file '%s' data (%ld bytes) exceeds available space at "
            "address %u (%u bytes)\n",
            path, data_size, load_addr, available);
    return -1;
  }

  size_t bytes_read = fread(&mem[load_addr], 1, data_size, f);
  if ((long)bytes_read != data_size) {
    fprintf(stderr, "Error reading PRG file '%s': incomplete read\n", path);
    return -1;
  }

  fprintf(stderr, "Loaded PRG '%s': %ld bytes at $%04X\n", path, data_size,
          load_addr);
  return 0;
}

/**
 * Parse hexadecimal byte from string (no validation)
 *
 * Args:
 *   str: pointer to 2 hex digits
 *
 * Returns:
 *   Byte value
 */
static uint8_t hex_byte(const char *str) {
  uint8_t val = 0;
  for (int i = 0; i < 2; i++) {
    val = (val << 4) | (isdigit(str[i]) ? (str[i] - '0')
                                         : (toupper(str[i]) - 'A' + 10));
  }
  return val;
}

/**
 * Validate and parse Intel HEX record
 *
 * Format: :LLAAAATTDD...CC
 *   LL = byte count, AAAA = address, TT = record type,
 *   DD... = data bytes, CC = checksum
 *
 * Args:
 *   line: input line
 *   out_data: output data buffer
 *   out_len: output data length
 *   out_addr: output address
 *   out_type: output record type
 *
 * Returns:
 *   0 on success, -1 on parse error
 */
static int parse_hex_record(const char *line, uint8_t *out_data,
                            size_t *out_len, uint16_t *out_addr,
                            uint8_t *out_type) {
  if (!line || line[0] != ':' || strlen(line) < 11) {
    return -1;
  }

  uint8_t byte_count = hex_byte(line + 1);
  *out_addr = (hex_byte(line + 3) << 8) | hex_byte(line + 5);
  *out_type = hex_byte(line + 7);

  /* Validate line length */
  size_t expected_len = 11 + (byte_count * 2);
  if (strlen(line) < expected_len) {
    return -1;
  }

  /* Calculate and verify checksum */
  uint8_t checksum = byte_count + (*out_addr >> 8) + (*out_addr & 0xFF) +
                     *out_type;
  for (int i = 0; i < byte_count; i++) {
    out_data[i] = hex_byte(line + 9 + (i * 2));
    checksum += out_data[i];
  }

  uint8_t file_checksum = hex_byte(line + 9 + (byte_count * 2));
  if ((uint8_t)(256 - checksum) != file_checksum) {
    return -1; /* Checksum mismatch */
  }

  *out_len = byte_count;
  return 0;
}

/**
 * Load HEX file (Intel HEX format)
 *
 * Args:
 *   f: open file handle
 *   mem: memory array
 *   path: file path (for error messages)
 *
 * Returns:
 *   0 on success, -1 on error
 */
static int load_hex_file(FILE *f, uint8_t *mem, const char *path) {
  char line[1024];
  uint16_t extended_addr = 0;
  int line_num = 0;
  bool eof_seen = false;

  while (fgets(line, sizeof(line), f) && !eof_seen) {
    line_num++;

    /* Strip trailing whitespace */
    for (int i = strlen(line) - 1; i >= 0 && isspace(line[i]); i--) {
      line[i] = '\0';
    }

    /* Skip empty lines */
    if (strlen(line) == 0) continue;

    uint8_t data[256];
    size_t data_len;
    uint16_t addr;
    uint8_t rec_type;

    if (parse_hex_record(line, data, &data_len, &addr, &rec_type) < 0) {
      fprintf(stderr, "Error: HEX file '%s' line %d: invalid record\n", path,
              line_num);
      return -1;
    }

    switch (rec_type) {
      case 0x00: /* Data record */
      {
        uint32_t full_addr = (extended_addr << 16) | addr;
        if (full_addr > 0xFFFF) {
          fprintf(stderr,
                  "Error: HEX file '%s' line %d: address $%06X out of range\n",
                  path, line_num, full_addr);
          return -1;
        }

        uint32_t available = 65536 - full_addr;
        if ((uint32_t)data_len > available) {
          fprintf(stderr,
                  "Error: HEX file '%s' line %d: data exceeds available space "
                  "at $%04X\n",
                  path, line_num, (uint16_t)full_addr);
          return -1;
        }

        memcpy(&mem[full_addr], data, data_len);
        break;
      }

      case 0x01: /* End of file */
        eof_seen = true;
        break;

      case 0x02: /* Extended segment address (not typically used for 6502) */
        if (data_len < 2) {
          fprintf(stderr,
                  "Error: HEX file '%s' line %d: invalid extended address "
                  "record\n",
                  path, line_num);
          return -1;
        }
        extended_addr = (data[0] << 8) | data[1];
        break;

      case 0x04: /* Extended linear address (upper 16 bits) */
        if (data_len < 2) {
          fprintf(stderr,
                  "Error: HEX file '%s' line %d: invalid extended address "
                  "record\n",
                  path, line_num);
          return -1;
        }
        extended_addr = (data[0] << 8) | data[1];
        break;

      default:
        /* Ignore unknown record types */
        break;
    }
  }

  if (!eof_seen) {
    fprintf(stderr, "Warning: HEX file '%s' missing end-of-file record\n",
            path);
  }

  fprintf(stderr, "Loaded HEX '%s': %d records parsed\n", path, line_num);
  return 0;
}

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
 *   address: load address (0-65535, used only for FORMAT_RAW)
 *   fmt: file format (FORMAT_AUTO to auto-detect)
 *
 * Returns:
 *   0 on success, -1 on error
 */
int load_config_add_file(struct load_config *config, const char *path,
                         uint16_t address, file_format_t fmt) {
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
  config->files[config->file_count].fmt = detect_format(path, fmt);
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

      if (load_config_add_file(config, path, address, FORMAT_AUTO) < 0) {
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

      if (load_config_add_file(config, path, address, FORMAT_AUTO) < 0) {
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
    file_format_t fmt = config->files[i].fmt;

    FILE *f = fopen(path, "rb");
    if (!f) {
      perror(path);
      return -1;
    }

    int result = 0;

    switch (fmt) {
      case FORMAT_PRG:
        result = load_prg_file(f, mem, path);
        break;

      case FORMAT_HEX:
        result = load_hex_file(f, mem, path);
        break;

      case FORMAT_RAW:
      case FORMAT_AUTO: /* Auto-detect failed, treat as RAW */
      {
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

        fprintf(stderr, "Loaded RAW '%s': %ld bytes at $%04X\n", path, file_size,
                address);
        break;
      }
    }

    fclose(f);

    if (result < 0) {
      return -1;
    }
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
 *   0xFFFF if reset vector should be used, otherwise the startup address
 */
uint16_t get_startup_addr(struct cpu *_unused, const struct load_config *config) {
  (void)_unused;
  if (!config) {
    return 0xFFFF; /* Default to reset vector */
  }

  if (!config->use_reset_vector) {
    return config->start_addr;
  }

  /* Default: use reset vector (requires calling rst()) */
  return 0xFFFF;
}

#endif /* BSOZ_LOADER_H */
