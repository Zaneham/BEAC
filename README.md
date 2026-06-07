# BEAC

The Burroughs Extended ALGOL Compiler.

A compiler for Burroughs Extended ALGOL, the dialect that ran on the
B5000/B5500/B6700/B7700 large systems, lowered all the way to native x86-64
and executed. Built off the 1969 B5500 Extended ALGOL Reference Manual, one
construct at a time.

It is a gift. Released freely under Apache 2.0 so anyone who wants to run, study,
or keep this language alive can do so without asking. It's also been sitting on my hardrive for way too long so here you go!

## Why this exists

Generic ALGOL has run in a lot of places. The *Burroughs dialect specifically*,
the one with partial-word designators, the `@` exponent marker, the stream and
string machinery, has only ever lived on Burroughs large systems and their
Unisys ClearPath descendants. To run it today you need Unisys iron or Unisys'
emulation of it. There is no `apt install`.

So this is the door out of that building. Burroughs ALGOL source goes in,
native machine code comes out, on a laptop, or a deskptop or anywhere that can run Doom. As far as I know it is the only compiler that does this, which is
reason enough to build it.

## A program it runs

```algol
BEGIN
  INTEGER PROCEDURE SUM(N); VALUE N; INTEGER N;
  BEGIN
    INTEGER PROCEDURE INNER; INNER := N;
    IF N <= 0 THEN SUM := 0
    ELSE BEGIN INTEGER T; T := SUM(N - 1); SUM := INNER + T END
  END;
  ...
END
```

`SUM` is recursive, and `INNER` is nested inside it and reads `SUM`'s
parameter `N`, after the recursive call has returned. Getting `SUM(3) = 6`
out of that is the whole reason the display exists (see below). It compiles
and runs.

## What works

- The full ALGOL-60 core of the dialect: blocks, `INTEGER`/`REAL`/`BOOLEAN`,
  the expression grammar with the manual's precedence (`*` is exponentiation,
  `TIMES` is multiply), word and symbol operator forms (`A LSS B` is `A < B`).
- `IF`/`THEN`/`ELSE`, `FOR ... STEP ... UNTIL ... DO`, `GO TO` and labels.
- One-dimensional arrays with constant bounds, indexed reads and writes.
- Procedures and typed functions (the function returns the value assigned to
  its own name), parameters by value, calls with arguments, and recursion.
- `REAL` arithmetic and `REAL`-valued functions, returned and captured
  correctly through the float path.
- **Non-local access via a software display.** A procedure nested inside
  another reads the enclosing procedure's variables, at any depth, and
  correctly under recursion, because each activation pushes its own record and
  the display saves and restores per level. This is the B6700's D-register
  mechanism rebuilt in software, following Organick's description of the
  machine. It is the part I am most pleased with.

## What it does not do yet

You gottah know before you start chucking code in:

- The `FOR ... WHILE` element form, negative `STEP`, and multi-element
  for-lists.
- Multi-dimensional arrays (the bound machinery is there; only 1-D is wired).
- Call-by-name and formal procedures. The display has a noted refinement it
  will need for these (following the static link rather than the caller).
- Burroughs I/O: `READ`, `WRITE`, `FORMAT`, the stream procedures.
- Bit-faithful Burroughs floating point (the 48-bit word with the octal
  exponent). The type bridge is already parameterised for it; only the native
  IEEE path is built.

## How it works

The front end (`src/fe/a_*`) is BEAC's own: lexer, parser, semantic analysis
with lexical-level tracking, a type bridge, and the lowerer. It produces a
flat SSA IR, which then runs through a register allocator and an x86-64
emitter inherited from Skyhawk, my JOVIAL compiler, since JOVIAL and Burroughs
ALGOL are cousins on the ALGOL-58 core and share the same machine-level needs.

The display lives in a global data area carved as
`[program globals | stack pointer | display[1..N] | activation-record stack]`.
A procedure with escaping variables gets a fixed-size activation record on
entry; its prologue parks the previous display entry for its level, points the
display at its own record, and bumps the stack pointer; its epilogue restores.
A non-local variable at level L is reached as `display[L] + offset`. All of it
sits on a single new IR primitive (the address of the global area); the rest
is just loads, stores, and adds.

## Building and running

```
make          # builds the beac compiler
make test     # builds and runs the test suite
```

Then point it at a program:

```
beac --run RUNIT program.alg   # JIT the named procedure and print what it returns
beac --ir program.alg          # dump the IR
beac -o program.obj program.alg # write a COFF object
```

For example, with the recursive-sum program from earlier saved as `sum.alg`,
`beac --run RUNIT sum.alg` prints the sum. The `--run` mode compiles the whole
way to machine code, drops it into executable memory, and calls it.

The test suite lowers ALGOL programs, JITs them, and checks the values they
compute, recursion, loops, arrays, nested non-local access, real arithmetic,
all running as native code.

## Sources

made with these documents:

- Burroughs B5500 Extended ALGOL Reference Manual (1969) for the language.
- Elliott Organick, *Computer System Organization: The B5700/B6700 Series*
  (1973) for the stack and display architecture.

## License

Apache 2.0. Use it, study it, build on it, ship it, no strings. See `LICENSE`.
