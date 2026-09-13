# Running the corpus

Build `bin/ez80asm`, then run from the repository root:

```sh
./test.sh
./test.sh -m
```

Each category keeps its own readable `test.sh` and assembly fixtures in `tests/`.
Only `.s` files are selected; include files should retain another extension.

## Positive cases

Add `name.s` and `name.expect`. The latter contains the expected raw output bytes.
The runner requires exit status zero and compares the binary when `.expect` exists.
A missing binary or comparison-tool failure is a failure, not a match.
Some historical smoke tests intentionally have no expected binary.

## Negative cases

Place the source in the appropriate `Errors_*` category. The runner requires
exit status 1 (an assembler error), and no remaining output binary. A crash or
missing executable must fail the test suite.

An optional `name.error` contains a single diagnostic substring, matched literally
and case-insensitively against the assembler output. Use this when a test should
verify a particular error rather than accepting any assembler error.

## Specialized fixup tests

`Fixups/test.sh` explicitly lists the existing integration and host-probe scripts.
The integration tests receive the selected assembler and command-line flags.
The host probes build their own binaries for allocator/I/O failure injection,
tiny output windows and numeric/tokenizer comparisons. They require Python 3 and
`cc` with AddressSanitizer and UndefinedBehaviorSanitizer support. Their internal
compiler/test options are intentionally independent of `./test.sh` arguments.

## Boundary fixtures

- `Opcodes/displacement_forward_boundaries`: IX/IY forward `.equ` values at
  -128, -1, 0 and 127, an expression, and stores with both operands deferred.
- `Errors_opcodes/forward_*`: deferred displacement outside either limit.
- `Labels/*_positive_limit` and `*_negative_limit`: JR, JR NZ and DJNZ at
  their exact signed-byte limits; corresponding overflow cases are negative tests.
- `Opcodes/forward_adl_switch` and `forward_suffix_adl_switch`: instruction
  widths remain those selected at emission despite later ADL changes.

Fixture paths above are relative to each category's `tests/` directory.
