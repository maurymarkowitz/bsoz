#ifndef BSOZ_STATS_H
#define BSOZ_STATS_H

#include <stdint.h>
#include <stdio.h>

/*
 * Opcode Statistics Collection Header
 *
 * This header provides functionality to track and report statistics about
 * 6502 opcode execution, including execution counts and cycle timing.
 * Statistics collection is optional and can be disabled at compile time
 * by setting ENABLE_STATS to 0.
 */

/* Single opcode statistics */
struct opcode_stats {
  uint32_t count;    /* number of times executed */
  uint32_t cycles;   /* total cycles spent in this opcode */
};

/* Container for all opcode statistics */
struct cpu_stats {
  struct opcode_stats opcodes[256];  /* per-opcode statistics */
  uint32_t total_cycles;             /* total cycle count */
  uint32_t total_instructions;       /* total instructions executed */
};

/**
 * Initialize statistics structure to zero
 *
 * Args:
 *   stats: pointer to cpu_stats structure to initialize
 */
void cpu_stats_init(struct cpu_stats *stats) {
  if (!stats) {
    return;
  }
  for (int i = 0; i < 256; i++) {
    stats->opcodes[i].count = 0;
    stats->opcodes[i].cycles = 0;
  }
  stats->total_cycles = 0;
  stats->total_instructions = 0;
}

/**
 * Update statistics for an executed opcode
 *
 * Args:
 *   stats: pointer to cpu_stats structure
 *   opcode: the 6502 opcode that was executed (0-255)
 *   cycles: number of cycles the opcode took
 */
void cpu_stats_update(struct cpu_stats *stats, uint8_t opcode, uint32_t cycles) {
  if (!stats) {
    return;
  }
  stats->opcodes[opcode].count++;
  stats->opcodes[opcode].cycles += cycles;
  stats->total_cycles += cycles;
  stats->total_instructions++;
}

/**
 * Reset statistics structure to zero
 *
 * Args:
 *   stats: pointer to cpu_stats structure to reset
 */
void cpu_stats_reset(struct cpu_stats *stats) {
  cpu_stats_init(stats);
}

/**
 * Generate a human-readable statistics report
 *
 * Args:
 *   stats: pointer to cpu_stats structure
 *   output: FILE pointer for output (e.g., stdout, stderr)
 */
void cpu_stats_report(struct cpu_stats *stats, FILE *output) {
  if (!stats || !output) {
    return;
  }

  fprintf(output, "\n");
  fprintf(output, "════════════════════════════════════════════════════════════\n");
  fprintf(output, "                   Opcode Statistics Report\n");
  fprintf(output, "════════════════════════════════════════════════════════════\n");
  fprintf(output, "\n");

  fprintf(output, "Total Instructions: %u\n", stats->total_instructions);
  fprintf(output, "Total Cycles:       %u\n", stats->total_cycles);
  if (stats->total_instructions > 0) {
    fprintf(output, "Average Cycles/Instruction: %.2f\n",
            (double)stats->total_cycles / stats->total_instructions);
  }
  fprintf(output, "\n");

  /* Find top 10 opcodes by execution count */
  fprintf(output, "Top 10 Opcodes by Execution Count:\n");
  fprintf(output, "─────────────────────────────────────────────────────────────\n");

  struct opcode_count_pair {
    uint8_t opcode;
    uint32_t count;
  } top_by_count[256];

  for (int i = 0; i < 256; i++) {
    top_by_count[i].opcode = i;
    top_by_count[i].count = stats->opcodes[i].count;
  }

  /* Simple bubble sort (256 items is small enough) */
  for (int i = 0; i < 256; i++) {
    for (int j = i + 1; j < 256; j++) {
      if (top_by_count[j].count > top_by_count[i].count) {
        struct opcode_count_pair temp = top_by_count[i];
        top_by_count[i] = top_by_count[j];
        top_by_count[j] = temp;
      }
    }
  }

  for (int i = 0; i < 10 && top_by_count[i].count > 0; i++) {
    uint8_t op = top_by_count[i].opcode;
    fprintf(output, "  #%d: 0x%02X  %10u executions  %10u cycles\n",
            i + 1, op, stats->opcodes[op].count, stats->opcodes[op].cycles);
  }

  fprintf(output, "\n");

  /* Top 10 opcodes by cycle usage */
  fprintf(output, "Top 10 Opcodes by Cycle Usage:\n");
  fprintf(output, "─────────────────────────────────────────────────────────────\n");

  struct opcode_cycle_pair {
    uint8_t opcode;
    uint32_t cycles;
  } top_by_cycles[256];

  for (int i = 0; i < 256; i++) {
    top_by_cycles[i].opcode = i;
    top_by_cycles[i].cycles = stats->opcodes[i].cycles;
  }

  /* Simple bubble sort */
  for (int i = 0; i < 256; i++) {
    for (int j = i + 1; j < 256; j++) {
      if (top_by_cycles[j].cycles > top_by_cycles[i].cycles) {
        struct opcode_cycle_pair temp = top_by_cycles[i];
        top_by_cycles[i] = top_by_cycles[j];
        top_by_cycles[j] = temp;
      }
    }
  }

  for (int i = 0; i < 10 && top_by_cycles[i].cycles > 0; i++) {
    uint8_t op = top_by_cycles[i].opcode;
    fprintf(output, "  #%d: 0x%02X  %10u cycles  %10u executions\n",
            i + 1, op, stats->opcodes[op].cycles, stats->opcodes[op].count);
  }

  fprintf(output, "\n");
  fprintf(output, "════════════════════════════════════════════════════════════\n");
  fprintf(output, "\n");
}

/**
 * Get cycle count for a specific 6502 instruction based on addressing mode
 *
 * This function returns the base cycle count for an instruction.
 * Note: Some instructions have variable cycle counts (e.g., branches,
 * indexed addressing with page crossing). The caller is responsible
 * for adjusting based on actual conditions.
 *
 * Args:
 *   opcode: the 6502 opcode (0-255)
 *
 * Returns:
 *   Cycle count for the opcode (2-6 for most instructions)
 */
uint8_t get_opcode_cycles(uint8_t opcode) {
  /* 6502 instruction cycle times (base counts) */
  static const uint8_t cycles[256] = {
    /* 0x00-0x0F */
    7, 6, 0, 8, 3, 3, 5, 5, 3, 2, 2, 2, 4, 4, 6, 6,
    /* 0x10-0x1F */
    2, 5, 0, 8, 4, 4, 6, 6, 2, 4, 2, 7, 4, 4, 7, 7,
    /* 0x20-0x2F */
    6, 6, 0, 8, 3, 3, 5, 5, 4, 2, 2, 2, 4, 4, 6, 6,
    /* 0x30-0x3F */
    2, 5, 0, 8, 4, 4, 6, 6, 2, 4, 2, 7, 4, 4, 7, 7,
    /* 0x40-0x4F */
    6, 6, 0, 8, 3, 3, 5, 5, 3, 2, 2, 2, 3, 4, 6, 6,
    /* 0x50-0x5F */
    2, 5, 0, 8, 4, 4, 6, 6, 2, 4, 2, 7, 4, 4, 7, 7,
    /* 0x60-0x6F */
    6, 6, 0, 8, 3, 3, 5, 5, 4, 2, 2, 2, 5, 4, 6, 6,
    /* 0x70-0x7F */
    2, 5, 0, 8, 4, 4, 6, 6, 2, 4, 2, 7, 4, 4, 7, 7,
    /* 0x80-0x8F */
    2, 6, 2, 6, 3, 3, 3, 3, 2, 2, 2, 2, 4, 4, 4, 4,
    /* 0x90-0x9F */
    2, 6, 0, 6, 4, 4, 4, 4, 2, 5, 2, 5, 5, 5, 5, 5,
    /* 0xA0-0xAF */
    2, 6, 2, 6, 3, 3, 3, 3, 2, 2, 2, 2, 4, 4, 4, 4,
    /* 0xB0-0xBF */
    2, 5, 0, 5, 4, 4, 4, 4, 2, 4, 2, 4, 4, 4, 4, 4,
    /* 0xC0-0xCF */
    2, 6, 2, 8, 3, 3, 5, 5, 2, 2, 2, 2, 4, 4, 6, 6,
    /* 0xD0-0xDF */
    2, 5, 0, 8, 4, 4, 6, 6, 2, 4, 2, 7, 4, 4, 7, 7,
    /* 0xE0-0xEF */
    2, 6, 2, 8, 3, 3, 5, 5, 2, 2, 2, 2, 4, 4, 6, 6,
    /* 0xF0-0xFF */
    2, 5, 0, 8, 4, 4, 6, 6, 2, 4, 2, 7, 4, 4, 7, 7,
  };

  return cycles[opcode];
}

#endif /* BSOZ_STATS_H */
