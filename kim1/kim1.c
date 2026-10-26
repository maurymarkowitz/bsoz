#define _POSIX_C_SOURCE 199309L

#include <bsoz.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#include "rom.h"
#include "../bsoz_loader.h"
#include "../bsoz_args.h"

int debug_enabled = 0;
int terminal_enabled = 0;

int k = 0x15; /* pending key from hexpad */
int c = -1;   /* pending char from tty */

/* Serial I/O state machine for KIM-1 RRIOT bit-bang serial */
struct serial {
  uint8_t tx_buf;    /* transmit buffer */
  uint8_t rx_buf;    /* receive buffer */
  uint8_t tx_count;  /* bits transmitted */
  uint8_t rx_count;  /* bits received */
  uint8_t tx_busy;   /* transmission in progress */
  uint8_t rx_busy;   /* reception in progress */
  uint8_t baud_div;  /* baud rate divider for timing */
  uint16_t baud_clk; /* clock counter for baud timing */
} serial = {0};

struct cpu cpu;

/* Statistics collection global - only defined when ENABLE_STATS is set */
#if ENABLE_STATS
struct cpu_stats step_stats = {0};
#endif
uint8_t mem[0x10000];
uint16_t memsz = 1024;

struct rriot {
  uint8_t ram[64];
  uint8_t reg[4]; /* RIOT registers: A, DA, B, DB */
  uint16_t inc;   /* timer multiplier */
  uint8_t irq;    /* timer interrupt flag */
  uint16_t count; /* timer counter */
  uint16_t value; /* timer initial value */
  uint64_t start; /* timer start in ns */
} r2, r3;
uint16_t rriot_inc[4] = {1, 8, 64, 1024};

/* now returns current timestamp in nanoseconds */
uint64_t now(void) {
  struct timespec tv;
  clock_gettime(CLOCK_REALTIME, &tv);
  return tv.tv_sec * 1000000000 + tv.tv_nsec;
}

/* timer starts a RRIOT timer with the given increment and start value */
void timer(struct rriot *r, uint16_t inc, uint8_t val) {
  r->inc = inc;
  r->irq = 0;
  r->count = r->value = val;
  r->start = now();
}

/* rriot updates a RRIOT module on every CPU step */
void rriot(struct rriot *r) {
  uint64_t ms = (now() - r->start) / 1000;
  if (r->inc == 0 || r->irq) return; /* timer is stopped or interrupted */
  if (r->value * r->inc <= ms) {
    r->irq = 0x80;
    r->count = 0;
  } else {
    r->count = r->start - ms;
  }
}

uint8_t r8(struct cpu *cpu, uint16_t addr) {
  (void)cpu;
  if (addr >= 0x1800 && addr <= 0x1bff) {
    return rriot3_rom[addr - 0x1800];
  } else if (addr >= 0x1c00 && addr <= 0x1fff) {
    return rriot2_rom[addr - 0x1c00];
  } else if (addr >= 0xff00) {
    return rriot2_rom[addr - 0xfc00];
  } else if (addr >= 0x1780 && addr <= 0x17bf) {
    return r3.ram[addr - 0x17c0];
  } else if (addr >= 0x17c0 && addr <= 0x17ff) {
    return r2.ram[addr - 0x17c0];
  } else if (addr >= 0x1700 && addr <= 0x173f) {
    /* RRIIOT 6530-003 */
    if (addr >= 0x1700 && addr <= 0x1703) {
      /* read registers */
      return r3.reg[addr - 0x1700];
    } else if (addr == 0x1706 || addr == 0x170e) {
      /* reset timer? */
      if (!r3.irq) return r3.count;
      timer(&r3, r3.inc, r3.value);
      r3.count = 0xff;
      return 0;
    } else if (addr == 0x1707) {
      /* return timer interrupt status */
      return r3.irq;
    }
  } else if (addr >= 0x1740 && addr <= 0x177f) {
    /* RRIIOT 6530-002 */
    if (addr == 0x1740) {
      uint8_t mask[] = {0xbf, 0xdf, 0xef, 0xf7, 0xfb, 0xfd, 0xfe};
      switch ((r2.reg[2] >> 1) & 0xf) {
        case 0: return k < 7 ? mask[k] : 0xff;
        case 1: return (k >= 7 && k < 14) ? mask[k - 7] : 0xff;
        case 2: return (k >= 14 && k < 21) ? mask[k - 14] : 0xff;
        case 3: return 0xff;
      }
      return 0x80;
    } else if (addr >= 0x1741 && addr <= 0x1743) {
      return r2.reg[addr - 0x1740];
    } else if (addr == 0x1746 || addr == 0x174e) {
      if (!r2.irq) return r2.count;
      timer(&r2, r2.inc, r2.value);
      return 0;
    } else if (addr == 0x1747) {
      return r2.irq;
    }
  } else if (addr < memsz) {
    return mem[addr];
  }
  return 0;
}

uint8_t w8(struct cpu *cpu, uint16_t addr, uint8_t b) {
  (void)cpu;
  if (addr >= 0x1780 && addr <= 0x17bf) {
    r3.ram[addr - 0x17c0] = b;
  } else if (addr >= 0x17c0 && addr <= 0x17ff) {
    r2.ram[addr - 0x17c0] = b;
  } else if (addr >= 0x1700 && addr <= 0x173f) {
    /* RRIOT 6530-003 */
    if (addr >= 0x1700 && addr <= 0x1703) {
      r3.reg[addr - 0x1700] = b;
    } else if (addr >= 1704 && addr <= 0x1707) {
      timer(&r3, rriot_inc[addr - 0x1704], b);
    }
  } else if (addr >= 0x1740 && addr <= 0x177f) {
    /* RRIOT 6530-002 */
    if (addr >= 0x1740 && addr <= 0x1743) {
      r2.reg[addr - 0x1740] = b;
      if (addr == 0x1740) {
        /* Port A write: start serial transmission if not already busy */
        if (!serial.tx_busy) {
          serial.tx_buf = b;
          serial.tx_count = 0;
          serial.tx_busy = 1;
        }
      } else if (addr == 0x1742) {
        /* Control register: could be used to enable/disable serial mode */
      }
    } else if (addr >= 0x1744 && addr <= 0x1747) {
      timer(&r2, rriot_inc[addr - 0x1744], b);
    }
  } else if (addr < memsz) {
    mem[addr] = b;
  }
  return b;
}

void set_raw(void) {
  struct termios term;
  tcgetattr(0, &term);
  term.c_lflag &= ~(ICANON | ECHO | ICRNL);
  tcsetattr(0, TCSANOW, &term);
  setbuf(stdin, NULL);
}

void reset_term(void) {
  struct termios term;
  tcgetattr(0, &term);
  term.c_lflag |= ICANON | ECHO | ICRNL;
  tcsetattr(0, TCSANOW, &term);
  setbuf(stdin, NULL);
}

/* handle built-in hex keyboard input */
void hexpad(void) {
  if (k == 0x15 && (read(0, &k, 1) == 1)) {
    if (k >= 'A' && k <= 'Z') k = k - 'A' + 'a';

    if (k >= '0' && k <= '9') {
      k = k - '0';
    } else if (k >= 'a' && k <= 'f') {
      k = k - 'a' + 10;
    } else if (k == '[') { /* address mode */
      k = 0x10;
    } else if (k == ']') { /* data mode */
      k = 0x11;
    } else if (k == ' ' || k == '+') { /* next address */
      k = 0x12;
    } else if (k == 'g') { /* Go */
      k = 0x13;
    } else if (k == 'p') { /* PC */
      k = 0x14;
    } else if (k == 'r') { /* Reset */
      rst(&cpu);
      k = 0x15;
    } else if (k == 't') { /* switch to tty mode */
      /* TODO: */
    } else {
      k = 0x15;
    }
  }
}

void tty(void) {
  /* Handle serial I/O through RRIOT Port A (0x1740) and Port B (0x1741).
   * KIM-1 serial uses bit-banged I/O:
   * - Port A bit 7 (MSB) = RxD input
   * - Port A bit 0 (LSB) = TxD output
   * - Transmission: 1 start bit + 8 data bits + 1 stop bit (11 bits total)
   * - Reception: same format
   * - Baud rate derived from RRIOT timer
   */

  /* Read input from stdin if available */
  uint8_t ch;
  if (c == -1 && read(0, &ch, 1) == 1) {
    c = ch;
  }

  /* Handle reception: load character into rx_buf when available */
  if (!serial.rx_busy && c != -1) {
    serial.rx_buf = c;
    serial.rx_count = 0;
    serial.rx_busy = 1;
    serial.baud_clk = 0;
    if (terminal_enabled) {
      fprintf(stdout, "[RX: %02X '%c'] ", c, (c >= 32 && c < 127) ? c : '?');
      fflush(stdout);
    }
    c = -1;
  }

  /* Handle transmission: send bits from tx_buf */
  if (serial.tx_busy) {
    serial.baud_clk++;
    /* Assume baud timing is controlled by calling frequency.
     * At typical step rate (~1MHz), we can send bits at 1200 baud.
     * This is a simplified bit-bang: just shift out bits */
    if (serial.tx_count < 11) {
      uint8_t bit;
      if (serial.tx_count == 0) {
        bit = 0; /* start bit */
      } else if (serial.tx_count <= 8) {
        bit = (serial.tx_buf >> (serial.tx_count - 1)) & 1;
      } else {
        bit = 1; /* stop bit */
      }
      /* Update Port A bit 0 (TxD) */
      r2.reg[1] = (r2.reg[1] & 0xFE) | bit;
      serial.tx_count++;
    } else {
      serial.tx_busy = 0;
      if (terminal_enabled) {
        fprintf(stdout, "[TX: %02X '%c'] ", serial.tx_buf, 
                (serial.tx_buf >= 32 && serial.tx_buf < 127) ? serial.tx_buf : '?');
        fflush(stdout);
      }
    }
  }

  /* Handle reception: accumulate bits from Port A bit 7 (RxD) */
  if (serial.rx_busy) {
    serial.baud_clk++;
    /* Sample RxD at bit boundaries */
    if (serial.rx_count < 11) {
      uint8_t bit = (r2.reg[1] >> 7) & 1;
      if (serial.rx_count > 0 && serial.rx_count <= 8) {
        serial.rx_buf |= (bit << (serial.rx_count - 1));
      }
      serial.rx_count++;
    } else {
      /* Reception complete, character is in serial.rx_buf */
      serial.rx_busy = 0;
      /* Could signal interrupt or make character available here */
    }
  }
}


/* Render digit b at position n (counted right-to-left) */
void display(int n, uint8_t b) {
  static int leds[6] = {0};
  int i;
  char digits[73]; /* 6 digits x 4 columns x 3 rows + \0 */
  if (leds[n] == b) return;
  leds[n] = b;
  memset(digits, ' ', sizeof(digits));
  digits[71] = digits[71 - 24] = digits[71 - 48] = '\n';
  for (i = 0; i < 6; i++) {
    int d = leds[5 - i];
    digits[4 * i + 1] = ((d & 0x01) ? '_' : ' ');
    digits[4 * i + 24] = ((d & 0x20) ? '|' : ' ');
    digits[4 * i + 25] = ((d & 0x40) ? '_' : ' ');
    digits[4 * i + 26] = ((d & 0x02) ? '|' : ' ');
    digits[4 * i + 48] = ((d & 0x10) ? '|' : ' ');
    digits[4 * i + 49] = ((d & 0x08) ? '_' : ' ');
    digits[4 * i + 50] = ((d & 0x04) ? '|' : ' ');
  }
  digits[72] = '\0';
  puts(digits);
}

int main(int argc, char *argv[]) {
  struct load_config loader_config;
  bool exclude_args[256] = {false};
  uint16_t start_addr = 0xFFFF;

  /* Enable debug output if --debug or -d flag is present */
  debug_enabled = has_debug_flag(argc, argv);

  /* Enable terminal capture if --terminal or -t flag is present */
  terminal_enabled = has_terminal_flag(argc, argv);

  /* Initialize loader config */
  load_config_init(&loader_config);

  /* Parse custom file loading arguments */
  if (parse_load_args(argc, argv, &loader_config, exclude_args) != 0) {
    fprintf(stderr, "Error parsing file arguments\n");
    return 1;
  }

  set_raw();
  atexit(reset_term);
  fcntl(0, F_SETFL, fcntl(0, F_GETFL) | O_NONBLOCK);

  /* Load files from configuration */
  if (load_files(mem, &loader_config) != 0) {
    fprintf(stderr, "Error loading files\n");
    load_config_free(&loader_config);
    return 1;
  }

  /* Get startup address from loader config, or use reset vector */
  start_addr = get_startup_addr(&cpu, &loader_config);

  mem[0x17fa] = mem[0x17fe] = 0;
  mem[0x17fb] = mem[0x17ff] = 0x1c;

  if (start_addr != 0xFFFF) {
    cpu.pc = start_addr;
  } else {
    rst(&cpu);
  }

  for (;;) {
    hexpad();
    tty();
    rriot(&r2);
    rriot(&r3);

    if (cpu.pc == 0x1f79 || cpu.pc == 0x1f90) {
      k = 0x15;
    } else if (cpu.pc == 0x1f56) {
      display(9 - (cpu.x >> 1), cpu.a);
      cpu.pc = 0x1f5e;
    }

    if ((step(&cpu)) < 0) {
      load_config_free(&loader_config);
      return 1;
    }
  }

  load_config_free(&loader_config);
  return 0;
}
