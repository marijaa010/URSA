# tools/

Utility scripts that wrap URSA for testing and validation.

## compare_solutions.py

Compares the number of models found by URSA's SAT path and SMT path on the
same input program. Three paths are exercised:

- **SAT** — `./src/ursa <file>` (clasp by default). Counts `--> Solution N`
  lines for `assert_all`, single-model assignments for plain `assert`, or
  detects `No solutions found`.
- **SMT subprocess** — `./src/ursa -smt <file>` produces an SMT-LIB QF_BV
  formula; the script then drives the `z3` binary in an all-SAT loop (each
  iteration is a fresh `z3` process).
- **SMT Python API** — same SMT-LIB formula, but loaded into a Python-side
  `z3.Solver()` (or `z3.Optimize()` when the source uses `minimize`/`maximize`).
  This avoids per-call subprocess startup and keeps Z3's incremental learned
  clauses between checks. Requires the `z3-solver` package — install with
  `pip install -r tools/requirements.txt`.

The all-SAT loop is: get a model, add a blocking clause that forbids that
exact assignment, repeat until `unsat`. For `minimize`/`maximize` programs
the loop is replaced by a single Z3 call with `(minimize var)` / `(maximize var)`
and `(get-objectives)`.

### Usage

```
python3 tools/compare_solutions.py <file.urs> [options]
```

Options:

- `-l, --length N` — bit length (default 8). Pass the same value you would
  give to `ursa -lN`.
- `--max N` — cap on the number of SMT models to enumerate per path
  (default 10000). Prevents the all-SAT loop from running forever.
- `--timeout S` — Z3 timeout per call, seconds (default 60).
- `--total-timeout S` — upper bound on the whole SMT all-SAT loop, seconds
  (default 60). Useful when a problem has more models than `--max` would
  reach in reasonable time.
- `--ursa PATH` — path to the `ursa` binary (default `src/ursa` relative to
  the repo root).
- `--z3 PATH` — path to the `z3` binary (default looks on `$PATH`).
- `--no-api` — skip the Z3 Python API path.
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

$ python3 tools/compare_solutions.py examples/CSP/queens1.urs --total-timeout 30
  SAT solutions: 92
  SMT solutions: 92
  SMT solutions: 92
MATCH: all paths found 92 solutions

$ python3 tools/compare_solutions.py examples/CSP/GolombRuler1.urs
  SAT solutions: 0
  SMT solutions: 1
  SMT solutions: 1
MISMATCH: SAT=0, SMT subprocess=1, SMT API=1
```

### Requirements

- `ursa` binary built (`cd src && make`).
- `z3` on `$PATH`. macOS: `brew install z3`.
- (Optional) Z3 Python bindings for the API column:
  `pip install -r tools/requirements.txt`. Without them the script still
  runs SAT + SMT subprocess; pass `--no-api` to silence the missing-API
  notice.
