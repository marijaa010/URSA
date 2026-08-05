# URSA

URSA (c) 2010

Version 4.0

URSA is a system for Uniform Reduction to SAT: i.e., for solving combinatorial
problem by reducing them to SATisfiability problem.

Author: 

Predrag Janicic, University of Belgrade

URL: http://www.matf.bg.ac.rs/~janicic/

## Licence

URSA is distributed under the GNU Public Licence, see file COPYING for details.

## Executables

The directory 'bin' stores a pre-built executable.

## Building from source code

URSA is written in C/C++ and was successfully built and run under Linux.

To build ursa:

> cd src

> make
	
If the build is successful, the executable output file 'ursa' resides in the src directory.

## Usage 

Usage: 

  ./ursa options

Options:

-l - sets the number of bits that represent numbers (e.g., -l10; the default value is 8)

-d - DIMACS output only

-q - quite mode (models are not printed out)

-s - selects an underlying solvers (e.g., -sargosat, -sclasp; the defaulf is clasp)

-smt - emit SMT-LIB QF_BV instead of running a SAT solver

-smtlogic=QF_BV or QF_LIA - choose the SMT-LIB logic (implies -smt; default QF_BV)

-smtsolve=z3 or cvc5 - solve the emitted formula in-process with the linked
   solver and print models formatted the same as the SAT path. For `assert_all`,
   URSA enumerates all satisfying models by adding blocking clauses between
   checks. Implies -smt. Composes with -smtlogic (e.g., -smtlogic=QF_LIA
   -smtsolve=z3). Available only if URSA was built with support for that solver
   (see "Building with SMT solvers" below).

-smtout=<path> - write SMT-LIB output to the given file.
   Implies -smt. Composes with -smtlogic, but cannot be combined with -smtsolve.

Example:

./ursa -l10 < ../examples/CSP/queens1.urs

NOTA BENE: If URSA is used with clasp as an underlying SAT solver and if some propositional
variable is irrelevant for the asserted constraint, then its different values are not
considered within the set of all models (so the set of models may not be as expected).

## Building with SMT solvers

`-smtsolve` links Z3 and/or cvc5 as libraries and solves in the same process;
no external solver binary is launched. Each solver is optional and selected at
build time (the default builds both):

  cd src
  make                              # link both Z3 and cvc5 (default)
  make CVC5_SUPPORT=0               # Z3 only
  make Z3_SUPPORT=0                 # cvc5 only
  make Z3_SUPPORT=0 CVC5_SUPPORT=0  # neither; -smt / -smtout still emit SMT-LIB

Linking against a solver needs its development files (headers and libraries),
not just an executable. Point URSA at the install prefixes if they are not in
the default location:

  make Z3_PREFIX=/path/to/z3 CVC5_PREFIX=/path/to/cvc5

Z3 is auto-detected via Homebrew (`brew --prefix z3`); both prefixes default to
`/usr/local` otherwise.

**Z3:**

- macOS: `brew install z3`
- Linux: `sudo apt install libz3-dev` (Debian/Ubuntu) or
         `sudo dnf install z3-devel` (Fedora)

**cvc5** (no package manager; use the official static release archives from
https://github.com/cvc5/cvc5/releases):

- macOS: `cvc5-macOS-arm64-static-gpl.zip` (Apple Silicon) or
         `cvc5-macOS-x86_64-static-gpl.zip` (Intel)
- Linux: `cvc5-Linux-x86_64-static-gpl.zip`

Extract the cvc5 archive and pass its directory as `CVC5_PREFIX` (it contains
`include/` and `lib/`).

If `-smtsolve` is used for a solver that was not compiled in, URSA prints a
clear error asking you to rebuild with the corresponding flag.

## SMT-LIB output

With the `-smt` flag URSA does not run a SAT solver. Instead it translates the
constraints into an SMT-LIB formula and prints it to standard output. The target
logic is chosen with `-smtlogic`:

- `QF_BV` (default) - emits the constraints as fixed-width bit-vector terms.
- `QF_LIA` - emits linear integer arithmetic over unbounded `Int` values.

If any array is accessed with a symbolic index, the logic is automatically
extended to `QF_ABV` or `QF_ALIA` accordingly (see below).

The resulting `.smt2` file can be fed to any SMT solver that supports the
chosen logic (Z3, cvc5, Boolector for QF_BV, ...).

Example: emit SMT-LIB to a file and solve `examples/Simple/system2unknowns.urs`
with Z3:

  ./ursa -smtout=out.smt2 < ../examples/Simple/system2unknowns.urs  
  z3 out.smt2

Or let URSA pipe directly to the solver without writing a file:

  ./ursa -smtsolve=z3 < ../examples/Simple/system2unknowns.urs

### Linear Integer Arithmetic mode (QF_LIA)

With `-smtlogic=QF_LIA` URSA emits QF_LIA (Linear Integer Arithmetic) instead
of QF_BV. In this mode, numeric variables are treated as unbounded integers
rather than fixed-width bit-vectors, and arithmetic follows standard integer
semantics without modular wrap-around.

  ./ursa -smtlogic=QF_LIA < ../examples/Simple/system2unknowns.urs

Free variables are declared with the `Int` sort, and arithmetic uses the
standard SMT-LIB operators (`+`, `-`, `*`, `<`, `<=`, `=`, `div`, `mod`).

Restrictions in QF_LIA mode. The following operators are rejected with a
clear error message because they have no counterpart in linear integer
arithmetic:

- Bitwise operators (`&`, `|`, `^`, `~`)
- Shift operators (`<<`, `>>`)
- Nonlinear multiplication of two symbolic variables (`x * y`); only
  multiplication by a ground constant is permitted
- Division and modulo by a symbolic value (`x / y`, `x % y`); only
  division and modulo by a ground constant are permitted

Semantic differences from QF_BV. URSA programs written for QF_BV assume
modular arithmetic on fixed-width unsigned values. In QF_LIA the same
program is interpreted over the mathematical integers, which changes the
solution space in two ways:

- Integer literals are not truncated to the width given by `-l`. A literal
  such as `300` remains `300`, whereas in QF_BV with `-l 8` it would wrap
  to `44`.
- Signed comparisons apply: `nx < 0` may be true for negative `nx`. Programs
  that rely on the unsigned domain should add explicit non-negativity
  constraints (e.g., `assert(nx >= 0)`) when run in QF_LIA mode.

### Arrays with symbolic indices

URSA arrays that are only ever indexed by ground constants (e.g.
`nA[3] = 5`) continue to be emitted as flat scalar cells `nA_3_`, `nA_4_`,
etc. No new syntax and no CLI flag are required.

Once an array is accessed with a *symbolic* index at any point in the
program, URSA switches that array to an SMT-LIB `(Array ...)` value.

| base logic | with arrays |
|------------|-------------|
| QF_BV      | QF_ABV      |
| QF_LIA     | QF_ALIA     |

Example - find `nj` such that `nA[nj] == 30`:

```
nA[0] = 10;
nA[1] = 20;
nA[2] = 30;
nA[3] = 40;
assert(nA[nj] == 30 && nj < 4);
```

Example - symbolic write, then search:

```
for (ni = 0; ni < 5; ni++) nA[ni] = ni * ni;
nA[nj] = 999;
assert(nA[3] == 999 && nj < 5);
```

The SAT path does not support symbolic indexing; use `-smt` or
`-smtlogic=QF_LIA` for programs that rely on it.
