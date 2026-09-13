#!/bin/bash
# Integration regressions use the selected assembler and command-line options.
set -e
for test in regression common-mnemonics suffixes simple-call symbols opcode-shifts numeric-filter line-boundary; do
    echo "Running $test"
    python3 "$test.py" "$ASMBIN" "$@"
done

# These probes build their own host binaries to inject allocation/I/O failures,
# shrink output windows, or compare tokenizer/numeric internals under sanitizers.
for test in blocks reservation test_window_io tokens numeric-contract literals; do
    echo "Running $test (host probe)"
    python3 "$test.py"
done
