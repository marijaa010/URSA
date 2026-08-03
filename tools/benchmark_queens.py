#!/usr/bin/env python3
"""Measure how the SAT and SMT paths scale on N-queens as N grows.

Generates a temporary URSA program per N, runs the SAT path and the
linked SMT solvers (Z3, cvc5) under a per-run wall-clock cap, and prints
a table of median wall times. --first solves for one board (assert),
--all enumerates every board (assert_all).
"""

import argparse
import statistics
import subprocess
import tempfile
import time
from pathlib import Path

TEMPLATE_ASSERT = """\
nDim={n};
bDomain = true;
bNoCapture = true;
for(ni=0; ni<nDim; ni++) {{
  bDomain &&= (n[ni]<nDim);
  for(nj=ni+1; nj<nDim; nj++)
    bNoCapture &&= n[ni]!=n[nj] && ni+n[nj]!=nj+n[ni] && ni+n[ni]!=nj+n[nj];
}}
assert(bDomain && bNoCapture);
"""

TEMPLATE_ASSERT_ALL = TEMPLATE_ASSERT.replace("assert(", "assert_all(")


def bits_for(n: int) -> int:
    b = 1
    while (1 << b) < n:
        b += 1
    return b


def run_once(cmd: list[str], stdin_path: Path,
             timeout: float) -> tuple[float, bool]:
    """Return (wall_seconds, timed_out) for one run of cmd."""
    t0 = time.perf_counter()
    try:
        with open(stdin_path) as f:
            subprocess.run(cmd, stdin=f, capture_output=True,
                           text=True, timeout=timeout)
    except subprocess.TimeoutExpired:
        return timeout, True
    return time.perf_counter() - t0, False


def median_run(cmd: list[str], stdin_path: Path, timeout: float,
               repeats: int) -> tuple[float, bool]:
    """Median wall time over `repeats` runs; stops early on a timeout."""
    times = []
    for _ in range(repeats):
        t, timed_out = run_once(cmd, stdin_path, timeout)
        if timed_out:
            return timeout, True
        times.append(t)
    return statistics.median(times), False


def fmt(t: float, timed_out: bool, timeout: float) -> str:
    return f">{timeout:.0f}s (TO)" if timed_out else f"{t*1000:9.1f} ms"


def main() -> None:
    p = argparse.ArgumentParser()
    p.add_argument("--ursa", default=str(Path(__file__).parent.parent / "src" / "ursa"))
    p.add_argument("--sizes", type=int, nargs="+",
                   default=[8, 10, 12, 14, 16, 18, 20])
    p.add_argument("--timeout", type=float, default=60.0,
                   help="per-run wall-clock cap in seconds (default: 60)")
    p.add_argument("--repeats", type=int, default=3,
                   help="runs per configuration, median reported (default: 3)")
    p.add_argument("--first", action="store_true",
                   help="find one satisfying board (uses assert)")
    p.add_argument("--all", dest="all_solutions", action="store_true",
                   help="enumerate all boards (uses assert_all)")
    args = p.parse_args()

    if args.first == args.all_solutions:
        args.first = True
    template = TEMPLATE_ASSERT if args.first else TEMPLATE_ASSERT_ALL
    mode = "first" if args.first else "all"

    print(f"Mode: {mode}, timeout: {args.timeout}s per run, "
          f"median of {args.repeats}")
    print()
    headers = ["N", "bits", "SAT", "Z3", "cvc5"]
    print(" ".join(f"{h:>14}" for h in headers))
    print(" ".join("-" * 14 for _ in headers))

    for n in args.sizes:
        bits = max(4, bits_for(n))
        with tempfile.NamedTemporaryFile(mode="w", suffix=".urs",
                                         delete=False) as tmp:
            tmp.write(template.format(n=n))
            urs_path = Path(tmp.name)
        try:
            base = [args.ursa, f"-l{bits}"]
            cells = [str(n), str(bits)]
            for extra in ([], ["-smtsolve=z3"], ["-smtsolve=cvc5"]):
                t, timed_out = median_run(base + extra, urs_path,
                                          args.timeout, args.repeats)
                cells.append(fmt(t, timed_out, args.timeout))
            print(" ".join(f"{c:>14}" for c in cells))
        finally:
            urs_path.unlink(missing_ok=True)


if __name__ == "__main__":
    main()
