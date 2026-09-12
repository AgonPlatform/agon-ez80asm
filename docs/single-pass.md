# Single-pass assembly and output fixups

Based on upstream commit `a712e6542731d00cad4f364200ea9f3809bb8502`.

The source traversal now parses and emits each source inclusion once. After that traversal, `resolveFixups()` evaluates only unresolved expressions and patches their output bytes. It never calls the source parser or instruction selector. Macro definitions are collected during their initial read rather than scanning and rewinding the input.

## Output memory and file offsets

`OUTPUT_BUFFERSIZE` in `src/config.h` defaults to `65536UL`. It controls one fixed binary-output window and can also be overridden at compilation, for example with `-DOUTPUT_BUFFERSIZE=7` for stress tests. The existing input buffering and `-m` option are retained independently.

A full window remains resident until another output byte arrives. Therefore, programs of **up to and including 65,536 bytes** are patched entirely in memory and written once at close. Larger programs spill full windows while assembling. During fixup, the same buffer loads aligned windows from the output file, flushing dirty windows before switching. Multi-byte fields can cross any window boundary, and the final partial window retains its exact length.

Fixups use **file offsets**, separately from logical assembly addresses. This accounts for the initial ORG, later ORG gaps, deferred DS/ALIGN padding, INCBIN, and RELOCATE. Existing trailing DS/ALIGN omission is preserved. Reads, writes, seeks and final closes are checked; failed binary output is removed through the existing cleanup policy.

## Deferred values and memory costs

Only unresolved expressions allocate fixup records. Each contains the expression, its original logical PC, file/line, local scope, macro expansion and anonymous-label position. Already resolved expressions require no fixup allocation. Known operands still use the original instruction tables and transformations.

Fixup kinds cover 8/16/24/32-bit data, repeated initializers, relative branches, index displacements, bit numbers, RST vectors and IM selectors. Repeated initializers use a single record, regardless of the number of elements. Fixups perform deferred range validation and retain original diagnostic locations. Records are freed after fixup or on assembly failure.

The **output buffer** is bounded; total assembler memory is not constant. Labels, macros, existing input buffers and pending expression records still use dynamic memory. `-x` reports the output-window size and peak fixup memory separately. Anonymous labels now have stable in-memory nodes instead of a temporary `.lbl` file.

Expressions that affect layout or assembly control must still be known immediately: ORG, alignment, reservation counts, EQU definitions, IF, ADL and similar directives. Forward EQU chains remain unsupported. An expression containing unresolved terms is syntax-checked without performing arithmetic on placeholders; division by zero is reported when the value can be evaluated.

## Listings

`-l` spools the listing during source traversal. After binary fixup, its object columns are corrected from the output window. This reads listing text, not assembly source. Only listing lines with changed bytes are rewritten. `-d` displays this corrected listing after fixup; without `-l`, its `.lst.tmp` spool is removed at close. No listing spool is created without these options.

Because include depth and macro usage are no longer discovered by an earlier source pass, listing source columns reserve fixed padding. Listing bytes and source text are retained, but whitespace and the timing of console output differ from the two-pass version. Listing generation intentionally incurs extra file work to keep memory bounded.

## Validation

The original 27 test groups pass, including bundled programs, in normal and minimum-input-memory modes and with listings enabled. The new `Fixups` group adds 25 cases with independent expected bytes, including 64 KB boundaries, multiple windows, macro/local/anonymous labels, relocation, repeated data, listings and deferred errors.

Additional checks:

- The 25 cases pass with `OUTPUT_BUFFERSIZE=7` and `-m -l`.
- The 25 cases pass under host AddressSanitizer and UndefinedBehaviorSanitizer, with UB failures made fatal. Leak checking is disabled because the existing program leaves its process-lifetime label/macro/input tables to process exit.
- `test_window_io.py` instruments the output stdio calls. It verifies no reads and one write for resident output, and tests cleanup after injected read/write/seek/close failures.
- The Agon target compiles and links using the installed agondev toolchain. Hardware execution and Agon SD-card performance have not been measured.

Run the checks from the repository root:

```sh
make
bash test.sh
bash test.sh -m -l
python3 tests/Fixups/test_window_io.py

# Keep host and Agon object files separate.
make -f Makefile-agon OBJDIR=obj-agon BINDIR=bin-agon
```

The new regression tools require Python 3; the I/O probe additionally requires a host C compiler (`CC`, default `cc`).

## Performance expectations

Avoiding source tokenization, instruction selection and macro expansion a second time reduces work for ordinary resolved-input workloads. Programs dominated by forward references pay for allocating fixups and reevaluating expressions; larger outputs also pay for window reads and writes. This is not a universal speedup. Host synthetic checks showed faster known-label and macro-heavy assembly, but slightly slower all-forward-reference assembly. Measure representative projects on Agon before drawing conclusions about SD-card speed or available heap.
