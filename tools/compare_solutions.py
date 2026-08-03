#!/usr/bin/env python3
"""
Compare SAT and SMT solution sets for an URSA program.

URSA solves SMT in-process via a linked solver (`-smtsolve=z3|cvc5`), so both
the SAT path and the SMT path print models in the same URSA format. This
script runs URSA in each mode and compares the resulting solution sets.

Usage:
    python3 tools/compare_solutions.py <file.urs> [-l BITS]
            [--smt-solver z3|cvc5|all] [--smt-logic QF_BV|QF_LIA]
            [--single-solution] [--timeout S] [--show-models]
"""

import argparse
import re
import subprocess
import sys
import tempfile
import time
from pathlib import Path


def plural(n: int, word: str) -> str:
    return word if n == 1 else word + "s"


def maybe_rewrite_single(file_path: Path, single: bool) -> Path:
    """If single is True, write a temp copy with assert_all -> assert."""
    if not single:
        return file_path
    src = file_path.read_text()
    rewritten = re.sub(r"\bassert_all\b", "assert", src)
    tmp = tempfile.NamedTemporaryFile(
        mode="w", suffix=".urs", delete=False, prefix="single_"
    )
    tmp.write(rewritten)
    tmp.close()
    return Path(tmp.name)


MODEL_RE = re.compile(r"^([a-zA-Z_]\w*(?:\[\d+\])*)\s*=\s*([^;\s]+);", re.MULTILINE)


def parse_ursa_output(out: str) -> tuple[int, list[dict[str, str]]]:
    """Parse URSA output (SAT or -smtsolve) into (count, models).

    Returns count -1 if the output could not be parsed."""
    if "No solutions found" in out or "[Number of solutions: 0]" in out:
        return 0, []
    if "--> Solution" in out:
        chunks = re.split(r"^--> Solution \d+\s*$", out, flags=re.MULTILINE)
        models = []
        for chunk in chunks[1:]:
            model = dict(MODEL_RE.findall(chunk))
            if model:
                models.append(model)
        if models:
            return len(models), models
    model = dict(MODEL_RE.findall(out))
    if model:
        return 1, [model]
    if re.search(r"^yes\b", out, re.MULTILINE):
        return 1, [{}]
    if re.search(r"^no\b", out, re.MULTILINE):
        return 0, []
    return -1, []


def run_ursa(ursa: str, file_path: Path, length: int,
             extra_args: list[str], timeout: float
             ) -> tuple[int, list[dict[str, str]], float, bool]:
    """Run URSA once. Return (count, models, wall_seconds, timed_out)."""
    cmd = [ursa, f"-l{length}"] + extra_args
    t0 = time.perf_counter()
    try:
        with open(file_path) as f:
            result = subprocess.run(cmd, stdin=f, capture_output=True,
                                    text=True, timeout=timeout)
    except subprocess.TimeoutExpired:
        return -1, [], timeout, True
    wall = time.perf_counter() - t0
    count, models = parse_ursa_output(result.stdout)
    return count, models, wall, False


def compare_model_sets(a: list[dict[str, str]], b: list[dict[str, str]]) -> str:
    """If two model lists differ as sets, return a human-readable diff."""
    def freeze(m):
        return frozenset(m.items())
    sa = {freeze(m) for m in a}
    sb = {freeze(m) for m in b}
    only_a = sa - sb
    only_b = sb - sa
    if not only_a and not only_b:
        return ""
    lines = []
    for m in sorted(only_a):
        lines.append("  only in SAT: " + ", ".join(f"{k}={v}" for k, v in sorted(m)))
    for m in sorted(only_b):
        lines.append("  only in SMT: " + ", ".join(f"{k}={v}" for k, v in sorted(m)))
    return "\n".join(lines)


def dump_models(models: list[dict[str, str]]) -> None:
    for i, m in enumerate(models, 1):
        if not m:
            print(f"    [{i}] (no free variables)")
        else:
            items = ", ".join(f"{k}={v}" for k, v in sorted(m.items()))
            print(f"    [{i}] {items}")


def fmt_count(c: int) -> str:
    return "timeout / unparsed" if c < 0 else str(c)


def main() -> None:
    p = argparse.ArgumentParser(
        description="Compare SAT vs SMT solution counts for an URSA program"
    )
    p.add_argument("file", type=Path, help="URSA program (.urs)")
    p.add_argument("-l", "--length", type=int, default=8,
                   help="bit length (default: 8)")
    p.add_argument("--smt-solver", choices=["z3", "cvc5", "all"], default="z3",
                   help="linked SMT solver(s) to compare against SAT")
    p.add_argument("--smt-logic", choices=["QF_BV", "QF_LIA"], default="QF_BV",
                   help="SMT-LIB logic for the SMT path (default QF_BV)")
    p.add_argument("-s", "--single-solution", action="store_true",
                   help="find only one solution (rewrites assert_all to assert)")
    p.add_argument("--timeout", type=float, default=60.0,
                   help="per-run wall-clock cap in seconds (default: 60)")
    p.add_argument("--ursa", default=str(Path(__file__).parent.parent / "src" / "ursa"),
                   help="path to ursa binary")
    p.add_argument("--show-models", action="store_true",
                   help="print every model found (otherwise only counts)")
    args = p.parse_args()

    if not args.file.exists():
        print(f"ERROR: file not found: {args.file}", file=sys.stderr)
        sys.exit(2)
    if not Path(args.ursa).exists():
        print(f"ERROR: ursa binary not found: {args.ursa}", file=sys.stderr)
        sys.exit(2)

    print(f"File:   {args.file}")
    print(f"Length: {args.length} bit")
    if args.single_solution:
        print("Mode:   single-solution (assert_all -> assert)")
    print(flush=True)

    effective = maybe_rewrite_single(args.file, args.single_solution)
    logic_args = ["-smtlogic=QF_LIA"] if args.smt_logic == "QF_LIA" else []
    solvers = ["z3", "cvc5"] if args.smt_solver == "all" else [args.smt_solver]

    print("Running SAT path...", flush=True)
    sat_c, sat_m, sat_t, sat_to = run_ursa(args.ursa, effective, args.length, [],
                                           args.timeout)
    print(f"  SAT solutions: {fmt_count(sat_c)}  ({sat_t*1000:.1f} ms)")
    if args.show_models:
        dump_models(sat_m)

    smt_results = {}
    for solver in solvers:
        print(f"Running SMT path ({solver}, {args.smt_logic}, linked)...", flush=True)
        c, m, t, to = run_ursa(args.ursa, effective, args.length,
                               [f"-smtsolve={solver}"] + logic_args, args.timeout)
        smt_results[solver] = (c, m, t, to)
        print(f"  SMT solutions: {fmt_count(c)}  ({t*1000:.1f} ms)")
        if args.show_models:
            dump_models(m)

    print()
    counts = {"SAT": sat_c}
    for solver in solvers:
        counts[f"SMT-{solver}"] = smt_results[solver][0]

    distinct = set(counts.values())
    if len(distinct) == 1 and sat_c >= 0:
        only = next(iter(distinct))
        print(f"MATCH: all paths found {only} {plural(only, 'solution')}")
        for solver in solvers:
            diff = compare_model_sets(sat_m, smt_results[solver][1])
            if diff:
                print(f"NOTE: SAT vs SMT-{solver} assignments differ:")
                print(diff)
        sys.exit(0)
    else:
        labels = ", ".join(f"{k}={fmt_count(v)}" for k, v in counts.items())
        print(f"MISMATCH: {labels}")
        sys.exit(1)


if __name__ == "__main__":
    main()
