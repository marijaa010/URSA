# tools/

Utility scripts that wrap URSA for testing and validation.

## compare_solutions.py

Compares the number of models found by URSA's SAT path and SMT path on the
same input program. Three paths are exercised:

- **SAT** — `./src/ursa <file>` (clasp by default). Counts `--> Solution N`
  lines for `assert_all`, single-model assignments for plain `assert`, or
  detects `No solutions found`.
- **SMT subprocess** — `./src/ursa -smt <file>` produces an SMT-LIB formula;
  the script then drives the chosen solver binary in an all-SAT loop (each
  iteration is a fresh solver process).
- **SMT Python API** — same SMT-LIB formula, but loaded into a Python-side
  solver instance. Avoids per-call subprocess startup and keeps incremental
  learned clauses between checks.

Two SMT solvers are supported: **Z3** and **cvc5**. Chosen with the
`--smt-solver` option.

The all-SAT loop is: get a model, add a blocking clause that forbids that
exact assignment, repeat until `unsat`. For `minimize`/`maximize` programs
the loop is replaced by a single solver call with `(minimize var)` /
`(maximize var)` and `(get-objectives)`.

### Usage

```
python3 tools/compare_solutions.py <file.urs> [options]
```

Options:

- `-l, --length N` — bit length (default 8). Pass the same value you would
  give to `ursa -lN`.
- `--max N` — cap on the number of SMT models to enumerate per path
  (default 10000). Prevents the all-SAT loop from running forever.
- `--timeout S` — solver timeout per call, seconds (default 60).
- `--total-timeout S` — upper bound on the whole SMT all-SAT loop, seconds
  (default 60). Useful when a problem has more models than `--max` would
  reach in reasonable time.
- `--ursa PATH` — path to the `ursa` binary (default `src/ursa` relative to
  the repo root).
- `--z3 PATH` — path to the `z3` binary (default `z3` on `$PATH`).
- `--cvc5 PATH` — path to the `cvc5` binary (default `cvc5` on `$PATH`).
- `--smt-solver z3|cvc5` — which SMT solver to use (default `z3`).
- `--no-api` — skip the Python API path.
- `--show-models` — print every model found (otherwise only counts).
- `--smt-logic QF_BV|QF_LIA` — choose the SMT-LIB logic used by the SMT paths
  (default `QF_BV`). Under `QF_LIA` free variables are declared with the `Int`
  sort and arithmetic is unbounded; operators that have no linear-integer
  counterpart (bitwise, shifts, nonlinear `var * var`) are rejected with an
  explicit error from URSA.

Exit code is 0 if all paths report the same count, 1 otherwise.

### Examples

```
$ python3 tools/compare_solutions.py examples/Simple/system2unknowns.urs
  SAT solutions: 1
  SMT solutions: 1
  SMT solutions: 1
MATCH: all paths found 1 solution

$ python3 tools/compare_solutions.py examples/CSP/queens1.urs \
    --smt-solver cvc5 --total-timeout 30
  SAT solutions: 92
  SMT solutions: 92
  SMT solutions: 92
MATCH: all paths found 92 solutions
```

## Installing the SMT solvers

The script needs at least one of Z3 or cvc5 installed. Instructions below
cover installation on macOS and Linux.

### Z3

- **macOS (Homebrew):** `brew install z3`
- **Linux (apt):** `sudo apt install z3`
- **Python API:** `pip install z3-solver`

Both the binary and the Python bindings are widely distributed. This is the
simplest solver to set up and is the default choice.

### cvc5

- **macOS/Linux (binary):** Download the latest release from
  [github.com/cvc5/cvc5/releases](https://github.com/cvc5/cvc5/releases). Pick
  the archive matching your architecture (e.g., `cvc5-macOS-arm64-static-gpl.zip`
  or `cvc5-Linux-static.zip`), extract it, and either place `bin/cvc5` on your
  `$PATH` or pass the full path via `--cvc5`.
- **Python API:** `pip install cvc5` (installs the `cvc5` module for Python).

The static-gpl build is self-contained (all dependencies included) and works
without extra configuration.

### Requirements

- `ursa` binary built (`cd src && make`).
- At least one SMT solver installed.
- (Optional) Solver-specific Python packages for the API path:
  - Z3: `pip install z3-solver`
  - cvc5: `pip install cvc5`

If the Python API package is not installed for the chosen solver, the
script still runs the SAT path and SMT subprocess path.
