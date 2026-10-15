Implementation Status
====================

Phase 1: Opcode Statistics Framework
------------------------------------

Status: COMPLETE

Implemented in [bsoz_stats.h](bsoz_stats.h):
- `struct cpu_stats`: Tracks execution count and cycles for all 256 opcodes
- `cpu_stats_init()`: Initialize statistics structure
- `cpu_stats_update()`: Update statistics after each instruction
- `cpu_stats_reset()`: Clear all statistics
- `cpu_stats_report()`: Generate formatted report with top-10 analysis
- `get_opcode_cycles()`: Lookup cycle count for any opcode

Features:
- Complete 6502 cycle timing table covering all addressing modes
- Counts total instructions and total cycles
- Calculates average cycles per instruction
- Generates top-10 reports sorted by execution count and cycle usage
- Ready to integrate into step() function with: `if (ENABLE_STATS) cpu_stats_update(&stats, opcode, cycles);`

Tests:
- [test/test_stats.c](test/test_stats.c): Unit tests for all statistics functions
  * Initialization, updates, resets, cycle lookups all pass
  * Report generation tested successfully

Integration ready: Add `struct cpu_stats stats;` to emulator main, call `cpu_stats_update()` from step(), and generate report with `cpu_stats_report()` at exit.


Phase 2: File Loading Framework
--------------------------------

Status: COMPLETE

Implemented in [bsoz_loader.h](bsoz_loader.h):
- `struct load_config`: Configuration for multiple file loads with startup control
- `struct file_load`: Individual file load specification
- `load_config_init()`: Initialize loader configuration
- `load_config_add_file()`: Add a file to load list
- `load_config_free()`: Free allocated memory
- `parse_file_spec()`: Parse "filename@address" syntax
- `parse_load_args()`: Parse command-line arguments (supports both positional and --load options)
- `load_files()`: Load all configured files into memory
- `get_startup_addr()`: Determine startup address from configuration

Features:
- Flexible command-line syntax:
  * Positional: `apple1 program.bin@0x1000`
  * Option with @: `apple1 --load program.bin@0x1000`
  * Option separate: `apple1 --load program.bin 0x1000`
  * Multiple files supported
  * Startup override: `--start 0x2000`
- Memory bounds checking prevents file overflow
- Proper error reporting with file paths
- Multi-file loading tested

Integration completed in:
- [apple1/apple1.c](apple1/apple1.c): Full integration with file loading
- [kim1/kim1.c](kim1/kim1.c): Full integration with file loading
- Both emulators now support loading programs at arbitrary addresses

Tests:
- [test/test_loader.c](test/test_loader.c): Unit tests for all loader functions
  * File spec parsing with/without addresses
  * Configuration management
  * Argument parsing (positional, --load with @, --load with separate address)
  * Startup address control
- [test/test_integration.c](test/test_integration.c): Integration tests
  * File loading and memory verification
  * Code execution verification
  * Multiple file loading

All tests pass successfully.


Phase 3: Integration with Emulators
------------------------------------

Status: PARTIALLY COMPLETE

Completed:
- ✅ File loading fully integrated into apple1.c and kim1.c
- ✅ Command-line argument parsing for --load and positional syntax
- ✅ Startup address control via --start option
- ✅ Memory management and cleanup

Remaining (Optional):
- Statistics collection during execution (ready to integrate)
- Real-time cycle counting for benchmarking
- Conditional compilation flags for enable/disable


Quick Start Examples
====================

Loading a program at address 0x1000:
```bash
./apple1/apple1 myprogram.bin@0x1000
./apple1/apple1 -l myprogram.bin@0x1000
./apple1/apple1 -l myprogram.bin 0x1000
```

Loading multiple files:
```bash
./apple1/apple1 loader.bin@0x0000 program.bin@0x2000
./apple1/apple1 -l boot.bin@0x0000 -l app.bin@0x1000
```

With startup address:
```bash
./apple1/apple1 program.bin@0x1000 --start 0x1000
```

Running test suite:
```bash
cd test
./test_stats           # Run statistics tests
./test_loader          # Run file loader tests
./test_integration     # Run integration tests
./test                 # Run 6502 emulator test
```


Known Limitations
=================

1. Statistics framework not yet hooked into step() function
   - Structure is ready, just needs one-line call at appropriate point
   - Compile-time flag recommended for enabling/disabling

2. No format support yet for PRG or Intel HEX files
   - Framework only loads raw binary files
   - Parsing support is on TODO list

3. Automatic address calculation not implemented
   - All files must specify explicit addresses

4. C99 warnings in -std=c89 mode
   - Code uses C99 features (bool, variable declarations in for loops)
   - Functions correctly but generates warnings during compilation
   - Can be compiled with -std=c99 without warnings


Architecture Notes
==================

Both new modules are implemented as header-only libraries to:
- Avoid linkage issues across platforms
- Keep dependencies minimal
- Maintain non-invasive integration
- Simplify building and testing

The bsoz_loader.h module:
- Requires struct cpu forward declaration
- Returns 0 for success, -1 for errors
- Manages all memory allocation internally
- Bounds checks against 64K address space

The bsoz_stats.h module:
- Self-contained cycle timing table
- No external dependencies
- Ready for optional integration
- Report generation to FILE* for flexibility
