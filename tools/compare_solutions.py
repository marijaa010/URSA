#!/usr/bin/env python3
"""
Usage:
    python3 tools/compare_solutions.py <file.urs> [-l BITS] [--max N] [--timeout S]
"""

import argparse
import re
import subprocess
import sys
import time
from pathlib import Path

try:
    import z3 as z3py
    HAVE_Z3PY = True
except ImportError:
    HAVE_Z3PY = False


def plural(n: int, word: str) -> str:
    return word if n == 1 else word + "s"


def maybe_rewrite_single(file_path: Path, single: bool) -> Path:
    """If single is True, write a temp copy of the program with assert_all → assert,
    and return its path. Otherwise return the original."""
    if not single:
        return file_path
    import tempfile
    src = file_path.read_text()
    rewritten = re.sub(r"\bassert_all\b", "assert", src)
    tmp = tempfile.NamedTemporaryFile(
        mode="w", suffix=".urs", delete=False, prefix="single_"
    )
    tmp.write(rewritten)
    tmp.close()
    return Path(tmp.name)


def run_sat(file_path: Path, length: int, ursa: str) -> tuple[int, list[dict[str, str]], dict]:
    """Run URSA in SAT mode, return (count, models, timings).
    timings has keys: wall, generation, solving (last two parsed from URSA output)."""
    t0 = time.perf_counter()
    with open(file_path) as f:
        result = subprocess.run(
            [ursa, f"-l{length}"],
            stdin=f,
            capture_output=True,
            text=True,
        )
    wall = time.perf_counter() - t0
    out = result.stdout

    timings = {"wall": wall}
    num = r"[\d.]+(?:[eE][-+]?\d+)?"
    m = re.search(rf"\[Formula generation:.*?total:\s*({num})s\]", out)
    if m:
        timings["generation"] = float(m.group(1))
    m = re.search(rf"\[Solving time:\s*({num})s\]", out)
    if m:
        timings["solving"] = float(m.group(1))

    if "No solutions found" in out or "[Number of solutions: 0]" in out:
        return 0, [], timings

    if "--> Solution " in out:
        chunks = re.split(r"^--> Solution \d+\s*$", out, flags=re.MULTILINE)
        models = []
        for chunk in chunks[1:]:
            model = dict(re.findall(r"^([a-zA-Z_]\w*(?:\[\d+\])*)\s*=\s*([^;\s]+);", chunk, re.MULTILINE))
            if model:
                models.append(model)
        return len(models), models, timings

    model = dict(re.findall(r"^([a-zA-Z_]\w*(?:\[\d+\])*)\s*=\s*([^;\s]+);", out, re.MULTILINE))
    if model:
        return 1, [model], timings

    if re.search(r"^yes\b", out, re.MULTILINE):
        return 1, [{}], timings
    if re.search(r"^no\b", out, re.MULTILINE):
        return 0, [], timings

    print("WARNING: could not parse SAT output, dumping:", file=sys.stderr)
    print(out, file=sys.stderr)
    return -1, [], timings


def _split_smtlib_forms(text: str) -> list[str]:
    """Split SMT-LIB text into top-level parenthesized forms.
    URSA's pretty-printer emits multi-line (assert (and ... ...)), so a
    line-by-line parse is not enough. This walks with balanced parentheses,
    respecting `|...|` quoted symbols and `; ...` line comments."""
    forms = []
    i = 0
    n = len(text)
    while i < n:
        c = text[i]
        if c == ';':                                  # skip line comment
            j = text.find('\n', i)
            i = n if j == -1 else j + 1
            continue
        if c == '(':
            depth = 1
            start = i
            i += 1
            in_quote = False
            while i < n and depth > 0:
                ch = text[i]
                if ch == '|':
                    in_quote = not in_quote
                elif not in_quote:
                    if ch == '(':   depth += 1
                    elif ch == ')': depth -= 1
                i += 1
            forms.append(text[start:i])
        else:
            i += 1
    return forms


def extract_smt2(file_path: Path, length: int, ursa: str):
    """
    Run URSA in -smt mode, parse the emitted SMT-LIB.
    Returns ((declarations, assertions, free_vars), trivial, optimize).
    """
    with open(file_path) as f:
        result = subprocess.run(
            [ursa, "-smt", f"-l{length}"],
            stdin=f,
            capture_output=True,
            text=True,
        )

    declarations = []
    assertions = []
    free_vars = []
    optimize = None
    trivial = None
    out = result.stdout

    # Handle trivial results emitted as plain-text lines (not parenthesized).
    for line in out.splitlines():
        if line.startswith("yes (trivially)"): trivial = True
        elif line.startswith("no (trivially)"): trivial = False

    # Walk the SMT-LIB output as balanced parenthesized forms so that
    # multi-line (assert ...) blocks from the pretty-printer are treated
    # as single logical forms.
    for form in _split_smtlib_forms(out):
        if form.startswith("(declare-fun"):
            declarations.append(form)
            m = re.match(r"\(declare-fun\s+(\S+)\s+\(\)", form)
            if m:
                free_vars.append(m.group(1))
        elif form.startswith("(assert"):
            assertions.append(form)
        elif form.startswith("(minimize") or form.startswith("(maximize"):
            optimize = form

    if trivial is not None and not assertions:
        return ([], [], []), trivial, None
    return (declarations, assertions, free_vars), None, optimize


def run_z3(smt2: str, z3: str, timeout: int) -> str:
    """Send SMT-LIB to Z3 via stdin, return its stdout."""
    result = subprocess.run(
        [z3, "-in"],
        input=smt2,
        capture_output=True,
        text=True,
        timeout=timeout,
    )
    return result.stdout


def quote_smt_symbol(name: str) -> str:
    """Wrap a name in |...| if it contains characters disallowed in simple symbols."""
    simple = set("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ"
                 "0123456789+-/*=%?!.$_~&^<>@")
    if name and all(c in simple for c in name):
        return name
    return "|" + name + "|"


def smt_value_to_decimal(value: str) -> str:
    """Convert an SMT-LIB value token to a decimal string for easier comparison.
    #x03 -> "3", #b101 -> "5", true -> "true", (_ bv3 8) -> "3"."""
    if value.startswith("#x"):
        return str(int(value[2:], 16))
    if value.startswith("#b"):
        return str(int(value[2:], 2))
    m = re.match(r"\(_ bv(\d+) \d+\)", value)
    if m:
        return m.group(1)
    return value


def parse_get_value(output: str) -> dict[str, str]:
    """
    Parse `(get-value (var1 var2 ...))` output, which looks like:
        ((var1 #x03) (var2 #x04))
    Returns dict {name: value-as-smtlib-token}.
    """
    out = output.strip()
    if out.startswith("sat") or out.startswith("unsat"):
        out = out.split("\n", 1)[1].strip() if "\n" in out else ""

    pairs = re.findall(
        r"\(\s*(\|[^|]+\||[^()\s|]+)\s+((?:#[xb][0-9a-fA-F]+)|true|false|\(_ bv\d+ \d+\))\s*\)",
        out,
    )
    return {(name[1:-1] if name.startswith("|") else name): value
            for name, value in pairs}


def count_smt(
    file_path: Path,
    length: int,
    ursa: str,
    z3: str,
    max_solutions: int,
    timeout: int,
    total_timeout: float = 60.0,
    single_solution: bool = False,
) -> tuple[int, list[dict[str, str]], dict]:
    t_start = time.perf_counter()
    t0 = time.perf_counter()
    (declarations, assertions, free_vars), trivial, optimize = extract_smt2(
        file_path, length, ursa
    )
    ursa_emit = time.perf_counter() - t0
    timings = {"ursa_emit": ursa_emit, "z3_total": 0.0, "z3_first": None, "z3_calls": 0}

    if trivial is True:
        timings["wall"] = time.perf_counter() - t_start
        return 1, [{}], timings
    if trivial is False:
        timings["wall"] = time.perf_counter() - t_start
        return 0, [], timings

    if optimize is not None:
        smt2 = (
            "\n".join(declarations)
            + "\n"
            + "\n".join(assertions)
            + "\n"
            + optimize
            + "\n(check-sat)\n"
            + "(get-value (" + " ".join(free_vars) + "))\n"
        )
        t0 = time.perf_counter()
        out = run_z3(smt2, z3, timeout)
        elapsed = time.perf_counter() - t0
        timings["z3_total"] = elapsed
        timings["z3_first"] = elapsed
        timings["z3_calls"] = 1
        timings["wall"] = time.perf_counter() - t_start
        first = out.strip().split("\n", 1)[0].strip()
        if first != "sat":
            return 0, [], timings
        raw_model = parse_get_value(out)
        decimal_model = {n: smt_value_to_decimal(v) for n, v in raw_model.items()}
        return 1, [decimal_model], timings

    if not free_vars:
        smt2 = "(set-logic QF_BV)\n" + "\n".join(assertions) + "\n(check-sat)\n"
        t0 = time.perf_counter()
        out = run_z3(smt2, z3, timeout)
        elapsed = time.perf_counter() - t0
        timings["z3_total"] = elapsed
        timings["z3_first"] = elapsed
        timings["z3_calls"] = 1
        timings["wall"] = time.perf_counter() - t_start
        return (1, [{}], timings) if "sat" in out.split() else (0, [], timings)

    base = (
        "(set-logic QF_BV)\n"
        + "\n".join(declarations)
        + "\n"
        + "\n".join(assertions)
        + "\n"
    )
    get_value = "(get-value (" + " ".join(free_vars) + "))\n"

    blocking: list[str] = []
    models: list[dict[str, str]] = []
    progress_every = 50
    hit_total_timeout = False
    while len(models) < max_solutions:
        if time.perf_counter() - t_start > total_timeout:
            hit_total_timeout = True
            break
        smt2 = base + "\n".join(blocking) + "\n(check-sat)\n" + get_value
        t0 = time.perf_counter()
        out = run_z3(smt2, z3, timeout)
        z3_elapsed = time.perf_counter() - t0
        timings["z3_total"] += z3_elapsed
        timings["z3_calls"] += 1
        if timings["z3_first"] is None:
            timings["z3_first"] = z3_elapsed

        first = out.strip().split("\n", 1)[0].strip()
        if first == "unsat":
            break
        if first != "sat":
            print("WARNING: unexpected Z3 output:", file=sys.stderr)
            print(out, file=sys.stderr)
            break

        raw_model = parse_get_value(out)
        if not raw_model:
            print("WARNING: could not parse Z3 model:", file=sys.stderr)
            print(out, file=sys.stderr)
            break

        eqs = " ".join(f"(= {quote_smt_symbol(n)} {v})" for n, v in raw_model.items())
        blocking.append(f"(assert (not (and {eqs})))")
        models.append({n: smt_value_to_decimal(v) for n, v in raw_model.items()})

        if single_solution:
            break

        if len(models) % progress_every == 0:
            elapsed = time.perf_counter() - t_start
            print(f"  ... {len(models)} models, {elapsed:.1f}s elapsed",
                  file=sys.stderr, flush=True)

    if hit_total_timeout:
        print(f"WARNING: hit --total-timeout {total_timeout}s; partial count",
              file=sys.stderr)
    elif len(models) == max_solutions:
        print(f"WARNING: reached --max {max_solutions}; may be more solutions",
              file=sys.stderr)
    timings["wall"] = time.perf_counter() - t_start
    return len(models), models, timings


def count_smt_api(
    file_path: Path,
    length: int,
    ursa: str,
    max_solutions: int,
    total_timeout: float = 60.0,
    single_solution: bool = False,
) -> tuple[int, list[dict[str, str]], dict]:
    t_start = time.perf_counter()
    t0 = time.perf_counter()
    (declarations, assertions, free_vars), trivial, optimize = extract_smt2(
        file_path, length, ursa
    )
    ursa_emit = time.perf_counter() - t0
    timings = {"ursa_emit": ursa_emit, "z3_total": 0.0, "z3_first": None, "z3_calls": 0}

    if trivial is True:
        timings["wall"] = time.perf_counter() - t_start
        return 1, [{}], timings
    if trivial is False:
        timings["wall"] = time.perf_counter() - t_start
        return 0, [], timings

    if optimize is not None:
        smt2_text = (
            "\n".join(declarations) + "\n" + "\n".join(assertions) + "\n" + optimize + "\n"
        )
        opt = z3py.Optimize()
        try:
            opt.from_string(smt2_text)
        except z3py.Z3Exception as e:
            print("WARNING: Z3 API failed to parse optimize SMT-LIB:", file=sys.stderr)
            for line in str(e).splitlines()[:5]:
                print(f"  {line}", file=sys.stderr)
            timings["wall"] = time.perf_counter() - t_start
            return -1, [], timings
        t0 = time.perf_counter()
        result = opt.check()
        elapsed = time.perf_counter() - t0
        timings["z3_total"] = elapsed
        timings["z3_first"] = elapsed
        timings["z3_calls"] = 1
        timings["wall"] = time.perf_counter() - t_start
        if result != z3py.sat:
            return 0, [], timings
        m = opt.model()
        decimal_model = {}
        for d in m.decls():
            val = m[d]
            decimal_model[d.name()] = _z3_value_to_decimal(val)
        return 1, [decimal_model], timings

    smt2_text = (
        "(set-logic QF_BV)\n"
        + "\n".join(declarations)
        + "\n"
        + "\n".join(assertions)
        + "\n"
    )

    solver = z3py.Solver()
    try:
        parsed = z3py.parse_smt2_string(smt2_text)
    except z3py.Z3Exception as e:
        print("WARNING: Z3 API failed to parse SMT-LIB:", file=sys.stderr)
        msg = str(e)
        # show only the first few errors; the rest is usually cascading noise
        for line in msg.splitlines()[:5]:
            print(f"  {line}", file=sys.stderr)
        timings["wall"] = time.perf_counter() - t_start
        return -1, [], timings
    for a in parsed:
        solver.add(a)

    var_consts = {}
    for d in solver.assertions():
        for sub in _collect_free_vars(d):
            var_consts.setdefault(sub.decl().name(), sub)

    models: list[dict[str, str]] = []
    if not var_consts:
        # No free variables — single check-sat decides everything.
        t0 = time.perf_counter()
        result = solver.check()
        elapsed = time.perf_counter() - t0
        timings["z3_total"] = elapsed
        timings["z3_first"] = elapsed
        timings["z3_calls"] = 1
        timings["wall"] = time.perf_counter() - t_start
        return (1, [{}], timings) if result == z3py.sat else (0, [], timings)

    progress_every = 50
    hit_total_timeout = False
    while len(models) < max_solutions:
        if time.perf_counter() - t_start > total_timeout:
            hit_total_timeout = True
            break
        t0 = time.perf_counter()
        result = solver.check()
        z3_elapsed = time.perf_counter() - t0
        timings["z3_total"] += z3_elapsed
        timings["z3_calls"] += 1
        if timings["z3_first"] is None:
            timings["z3_first"] = z3_elapsed

        if result == z3py.unsat:
            break
        if result != z3py.sat:
            print(f"WARNING: Z3 API returned {result}", file=sys.stderr)
            break

        m = solver.model()
        raw_model = {}
        decimal_model = {}
        for name, const in var_consts.items():
            val = m.eval(const, model_completion=True)
            raw_model[name] = val
            decimal_model[name] = _z3_value_to_decimal(val)

        blocking = z3py.Not(
            z3py.And([const == raw_model[name] for name, const in var_consts.items()])
        )
        solver.add(blocking)
        models.append(decimal_model)

        if single_solution:
            break

        if len(models) % progress_every == 0:
            elapsed = time.perf_counter() - t_start
            print(f"  ... {len(models)} models, {elapsed:.1f}s elapsed",
                  file=sys.stderr, flush=True)

    if hit_total_timeout:
        print(f"WARNING: hit --total-timeout {total_timeout}s; partial count",
              file=sys.stderr)
    elif len(models) == max_solutions:
        print(f"WARNING: reached --max {max_solutions}; may be more solutions",
              file=sys.stderr)
    timings["wall"] = time.perf_counter() - t_start
    return len(models), models, timings


def _collect_free_vars(expr):
    """Walk a Z3 AST iteratively, yield uninterpreted constants (free variables).
    URSA assertions are deeply right-nested (and (and (and ...))), which blows
    the default Python recursion limit (1000) — so use an explicit stack."""
    seen = set()
    stack = [expr]
    while stack:
        e = stack.pop()
        key = e.get_id()
        if key in seen:
            continue
        seen.add(key)
        if z3py.is_const(e) and e.decl().kind() == z3py.Z3_OP_UNINTERPRETED:
            yield e
            continue
        stack.extend(e.children())


def _z3_value_to_decimal(val) -> str:
    """Convert a Z3 model value to a decimal string."""
    if z3py.is_bv_value(val):
        return str(val.as_long())
    if z3py.is_true(val):
        return "true"
    if z3py.is_false(val):
        return "false"
    return str(val)


def main():
    parser = argparse.ArgumentParser(
        description="Compare SAT vs SMT solution counts for an URSA program"
    )
    parser.add_argument("file", type=Path, help="URSA program (.urs)")
    parser.add_argument("-l", "--length", type=int, default=8,
                        help="bit length (default: 8)")
    parser.add_argument("--max", type=int, default=10000,
                        help="max SMT solutions to enumerate (default: 10000)")
    parser.add_argument("--timeout", type=int, default=60,
                        help="Z3 timeout per call, seconds (default: 60)")
    parser.add_argument("--total-timeout", type=float, default=60.0,
                        help="upper bound on the whole SMT all-SAT loop, seconds (default: 60)")
    parser.add_argument("--ursa", default=str(Path(__file__).parent.parent / "src" / "ursa"),
                        help="path to ursa binary")
    parser.add_argument("--z3", default="z3", help="path to z3 binary")
    parser.add_argument("--no-api", action="store_true",
                        help="skip the Z3 Python API comparison")
    parser.add_argument("--show-models", action="store_true",
                        help="print every model found (otherwise only counts)")
    parser.add_argument("-s", "--single-solution", action="store_true",
                        help="find only one solution (rewrites assert_all to assert) — "
                             "useful to measure 'find one' performance fairly, since Z3 has "
                             "no native all-SAT and is at a disadvantage in enumeration mode")
    args = parser.parse_args()

    if not args.file.exists():
        print(f"ERROR: file not found: {args.file}", file=sys.stderr)
        sys.exit(2)
    if args.file.suffix.lower() == ".smt2":
        print(f"ERROR: {args.file} looks like an SMT-LIB file (.smt2).", file=sys.stderr)
        print("       This script expects an URSA program (.urs); the .smt2 is what", file=sys.stderr)
        print("       URSA produces internally and is not parseable by URSA itself.", file=sys.stderr)
        sys.exit(2)
    if not Path(args.ursa).exists():
        print(f"ERROR: ursa binary not found: {args.ursa}", file=sys.stderr)
        sys.exit(2)

    print(f"File:   {args.file}")
    print(f"Length: {args.length} bit")
    if args.single_solution:
        print("Mode:   single-solution (assert_all → assert; SMT loop stops after 1)")
    print(flush=True)

    effective_file = maybe_rewrite_single(args.file, args.single_solution)

    print("Running SAT path...")
    sat_count, sat_models, sat_t = run_sat(effective_file, args.length, args.ursa)
    print(f"  SAT solutions: {sat_count}")
    if args.show_models:
        dump_models(sat_models)

    print("Running SMT path (subprocess: z3 binary)...", flush=True)
    smt_count, smt_models, smt_t = count_smt(
        effective_file, args.length, args.ursa, args.z3, args.max, args.timeout,
        args.total_timeout, args.single_solution,
    )
    print(f"  SMT solutions: {smt_count}")
    if args.show_models:
        dump_models(smt_models)

    api_count, api_models, api_t = None, None, None
    if HAVE_Z3PY and not args.no_api:
        print("Running SMT path (Z3 Python API)...", flush=True)
        api_count, api_models, api_t = count_smt_api(
            effective_file, args.length, args.ursa, args.max, args.total_timeout,
            args.single_solution,
        )
        print(f"  SMT solutions: {api_count}")
        if args.show_models:
            dump_models(api_models)
    elif not HAVE_Z3PY:
        print("(Z3 Python API not installed; run: pip install z3-solver)")

    print()
    print("Timing:")
    print(f"  SAT             wall: {sat_t['wall']*1000:9.2f} ms")
    if "generation" in sat_t:
        print(f"                  gen:  {sat_t['generation']*1000:9.2f} ms")
    if "solving" in sat_t:
        print(f"                  solv: {sat_t['solving']*1000:9.2f} ms")
    print(f"  SMT subprocess  wall: {smt_t['wall']*1000:9.2f} ms")
    print(f"                  emit: {smt_t['ursa_emit']*1000:9.2f} ms")
    print(f"                  z3:   {smt_t['z3_total']*1000:9.2f} ms over {smt_t['z3_calls']} {plural(smt_t['z3_calls'], 'call')}")
    if smt_t["z3_first"] is not None:
        print(f"                  1st:  {smt_t['z3_first']*1000:9.2f} ms (first solution)")
    if api_t is not None:
        print(f"  SMT Python API  wall: {api_t['wall']*1000:9.2f} ms")
        print(f"                  emit: {api_t['ursa_emit']*1000:9.2f} ms")
        print(f"                  z3:   {api_t['z3_total']*1000:9.2f} ms over {api_t['z3_calls']} {plural(api_t['z3_calls'], 'call')}")
        if api_t["z3_first"] is not None:
            print(f"                  1st:  {api_t['z3_first']*1000:9.2f} ms (first solution)")
    if sat_t.get("solving") is not None:
        print()
        print("Ratios vs SAT solving:")
        if smt_t["z3_first"] is not None and sat_t["solving"] > 0:
            print(f"  subprocess z3 first / SAT solving: {smt_t['z3_first']/sat_t['solving']:.1f}x")
        if api_t is not None and api_t["z3_first"] is not None and sat_t["solving"] > 0:
            print(f"  api z3 first / SAT solving:        {api_t['z3_first']/sat_t['solving']:.1f}x")
        if api_t is not None and smt_t["z3_first"] is not None and api_t["z3_first"] is not None and api_t["z3_first"] > 0:
            print(f"  subprocess overhead vs api (first call): {smt_t['z3_first']/api_t['z3_first']:.1f}x")

    print()
    counts = {"SAT": sat_count, "SMT subprocess": smt_count}
    if api_count is not None:
        counts["SMT API"] = api_count
    distinct = set(counts.values())
    if len(distinct) == 1:
        only = next(iter(distinct))
        print(f"MATCH: all paths found {only} {plural(only, 'solution')}")
        diff = compare_model_sets(sat_models, smt_models)
        if diff:
            print("NOTE: SAT vs SMT subprocess assignments differ:")
            print(diff)
        if api_models is not None:
            diff = compare_model_sets(sat_models, api_models)
            if diff:
                print("NOTE: SAT vs SMT API assignments differ:")
                print(diff)
        sys.exit(0)
    else:
        labels = ", ".join(f"{k}={v}" for k, v in counts.items())
        print(f"MISMATCH: {labels}")
        sys.exit(1)


def dump_models(models: list[dict[str, str]]) -> None:
    """Pretty-print a list of models."""
    for i, m in enumerate(models, 1):
        if not m:
            print(f"    [{i}] (no free variables)")
        else:
            items = ", ".join(f"{k}={v}" for k, v in sorted(m.items()))
            print(f"    [{i}] {items}")


def compare_model_sets(a: list[dict[str, str]], b: list[dict[str, str]]) -> str:
    """If two model lists differ as sets, return a human-readable diff. Else ''."""
    def freeze(m: dict[str, str]) -> frozenset:
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


if __name__ == "__main__":
    main()
