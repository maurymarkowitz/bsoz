/**
 * \file bsoz_args.h
 * \brief Command-line argument parsing utilities for BSOZ emulators
 *
 * Provides helper functions to parse common command-line flags like --debug and --terminal.
 * This header is self-contained and can be included in any main program.
 */

#ifndef BSOZ_ARGS_H
#define BSOZ_ARGS_H

#include <string.h>

/**
 * \brief Check if a debug flag is present in command-line arguments
 *
 * Searches for --debug or -d flags in argv and returns 1 if found, 0 otherwise.
 * This is used to enable debug output at runtime.
 *
 * \param argc Argument count from main()
 * \param argv Argument array from main()
 * \return 1 if --debug or -d flag found, 0 otherwise
 */
static int has_debug_flag(int argc, char *argv[]) {
  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--debug") == 0 || strcmp(argv[i], "-d") == 0) {
      return 1;
    }
  }
  return 0;
}

/**
 * \brief Check if a terminal capture flag is present in command-line arguments
 *
 * Searches for --terminal or -t flags in argv and returns 1 if found, 0 otherwise.
 * This is used to enable terminal I/O capture (logging of sent/received characters).
 *
 * \param argc Argument count from main()
 * \param argv Argument array from main()
 * \return 1 if --terminal or -t flag found, 0 otherwise
 */
static int has_terminal_flag(int argc, char *argv[]) {
  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--terminal") == 0 || strcmp(argv[i], "-t") == 0) {
      return 1;
    }
  }
  return 0;
}

#endif /* BSOZ_ARGS_H */
