#include <bsoz.h>
#include <fcntl.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <termios.h>
#include <unistd.h>

#include "rom.h"
#include "../bsoz_loader.h"

uint8_t mem[0x10000];
uint16_t memsz = 0xffff;
uint8_t ch;

uint8_t r8(struct cpu *c, uint16_t addr) {
  (void)c;
  if (!ch) {
    if (addr == 0xd010) return 0x80;
    if (addr == 0xd011) return 0x00;
  } else {
    if (addr == 0xd011) return 0x80;
    if (addr == 0xd010) {
      uint8_t tchar = ch;
      if (tchar >= 'a' && tchar <= 'z') { tchar = tchar - 'a' + 'A'; }
      if (tchar == '\n') tchar = '\r';
      ch = 0;
      return tchar | 0x80;
    }
  }
  if ((addr & 0xff00) == 0xff00) { return wozmon[addr & 0xff]; }
  if ((addr & 0xe000) == 0xe000) { return a1basic[addr & 0xfff]; }
  if (addr < memsz) { return mem[addr]; }
  return 0;
}

int col = 0;
uint8_t w8(struct cpu *c, uint16_t addr, uint8_t b) {
  (void)c;
  if (addr == 0xd012 || addr == 0xd0f2) {
    char o = b & 0x7f;
    col++;
    if (o == '\r') {
      o = '\n';
      col = 0;
    }
    if (o == 0x5f) o = '\b';
    printf("%c", o);
    if (col == 40) {
      printf("\n");
      col = 0;
    }
    fflush(stdout);
  } else {
    if (addr > memsz) return b;
    mem[addr] = b;
  }
  return b;
}

char *pre = NULL;
size_t presz = 0;
size_t preidx = 0;

void set_raw(void) {
  struct termios term;
  tcgetattr(0, &term);
  term.c_lflag &= ~ICANON;
  term.c_lflag &= ~ECHO;
  term.c_iflag &= ~ICRNL;
  tcsetattr(0, TCSANOW, &term);
  setbuf(stdin, NULL);
}

void reset_term(void) {
  struct termios term;
  tcgetattr(0, &term);
  term.c_lflag |= ICANON;
  term.c_lflag |= ECHO;
  term.c_iflag |= ICRNL;
  tcsetattr(0, TCSANOW, &term);
  setbuf(stdin, NULL);
}

int main(int argc, char *argv[]) {
  int c;
  struct cpu cpu = {0};
  struct load_config loader_config;
  bool exclude_args[256] = {false};
  uint16_t start_addr = 0xFFFF;

  /* Initialize loader config */
  load_config_init(&loader_config);

  /* Parse custom file loading arguments */
  if (parse_load_args(argc, argv, &loader_config, exclude_args) != 0) {
    fprintf(stderr, "Error parsing file arguments\n");
    return 1;
  }

  while ((c = getopt(argc, argv, "48l:")) != -1) {
    switch (c) {
      case '4': memsz = 4096; break;
      case '8': memsz = 8192; break;
      case 'l': {
        char path[512];
        uint16_t address;
        if (parse_file_spec(optarg, path, sizeof(path), &address) != 0) {
          fprintf(stderr, "Invalid file spec: %s\n", optarg);
          return 1;
        }
        if (load_config_add_file(&loader_config, path, address) != 0) {
          fprintf(stderr, "Error adding file: %s\n", path);
          return 1;
        }
        break;
      }
      case '?':
      default:
        fprintf(stderr, "USAGE: %s [-4|-8] [-l file[@addr]|file[@addr] ...]\n", argv[0]);
        fprintf(stderr, "       -4        Use 4K memory\n");
        fprintf(stderr, "       -8        Use 8K memory\n");
        fprintf(stderr, "       -l FILE   Load FILE (optionally at address after @)\n");
        load_config_free(&loader_config);
        exit(1);
    }
  }

  if (optind < argc) {
    FILE *f = fopen(argv[optind], "r");
    if (f == NULL) {
      perror(argv[1]);
      load_config_free(&loader_config);
      return 1;
    }
    fseek(f, 0, SEEK_END);
    presz = ftell(f);
    pre = calloc(1, presz);
    fseek(f, 0, SEEK_SET);
    if (fread(pre, presz, 1, f) < 1) {
      perror(argv[1]);
      load_config_free(&loader_config);
      return 1;
    }
    fclose(f);
  }

  /* Load files from configuration */
  if (load_files(mem, &loader_config) != 0) {
    fprintf(stderr, "Error loading files\n");
    load_config_free(&loader_config);
    return 1;
  }

  /* Get startup address from loader config, or use reset vector */
  start_addr = get_startup_addr(&cpu, &loader_config);

  set_raw();
  atexit(reset_term);
  fcntl(0, F_SETFL, fcntl(0, F_GETFL) | O_NONBLOCK);

  if (start_addr != 0xFFFF) {
    cpu.pc = start_addr;
  } else {
    rst(&cpu);
  }
  for (;;) {
    int op;
    if ((op = step(&cpu)) < 0) {
      printf("invalid instruction %02x at pc=%04x\n", op, cpu.pc);
      load_config_free(&loader_config);
      return 1;
    }
    if (!ch && preidx < presz) ch = pre[preidx++];
    if (!ch && (read(0, &ch, 1) == 1)) {
      if (ch == 0x08 || ch == 0x7f) {
        ch = 0x5F;
      } else if (ch == '') {
        if (start_addr != 0xFFFF) {
          cpu.pc = start_addr;
        } else {
          rst(&cpu);
        }
        ch = 0;
      }
    }
  }

  load_config_free(&loader_config);
  return 0;
}
