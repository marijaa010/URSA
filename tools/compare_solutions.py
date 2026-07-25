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

try:
    import cvc5 as cvc5py
    HAVE_CVC5PY = True
except ImportError:
    HAVE_CVC5PY = False


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

    if "No solutions found" in out or "[Number of solutions: 0]" in out:
        return 0, [], timings

    if re.search(r"^yes\b", out, re.MULTILINE):
        return 1, [{}], timings
    if re.search(r"^no\b", out, re.MULTILINE):
        return 0, [], timings

    print("WARNING: could not parse SAT output, dumping:", file=sys.stderr)
    print(out, file=sys.stderr)
    return -1, [], timings


def _split_smtlib_forms(text: str) -> list[str]:
    """Split SMT-LIB text into top-level parenthesized forms."""
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


def extract_smt2(file_path: Path, length: int, ursa: str, logic: str = "QF_BV"):
    """
    Run URSA in -smt mode with the given SMT-LIB logic (QF_BV or QF_LIA),
    parse the emitted SMT-LIB. Returns ((declarations, assertions, free_vars,
    emitted_logic), trivial, optimize).

    emitted_logic is what URSA actually put in (set-logic ...), which may
    differ from the requested logic (e.g., QF_BV upgraded to QF_ABV when
    an array with symbolic index is present, or QF_LIA to QF_ALIA).
    """
    ursa_args = [ursa, f"-l{length}"]
    if logic == "QF_LIA":
        ursa_args.append("-smtlogic=QF_LIA")
    else:
        ursa_args.append("-smt")
    with open(file_path) as f:
        result = subprocess.run(
            ursa_args,
            stdin=f,
            capture_output=True,
            text=True,
        )

    declarations = []
    assertions = []
    free_vars = []
    optimize = None
    trivial = None
    emitted_logic = logic
    out = result.stdout

    for line in out.splitlines():
        if line.startswith("yes (trivially)"): trivial = True
        elif line.startswith("no (trivially)"): trivial = False

    for form in _split_smtlib_forms(out):
        if form.startswith("(set-logic"):
            m = re.match(r"\(set-logic\s+(\S+?)\s*\)", form)
            if m:
                emitted_logic = m.group(1)
        elif form.startswith("(declare-fun"):
            declarations.append(form)
            m = re.match(r"\(declare-fun\s+(\S+)\s+\(\)\s+(.+)\)\s*$", form, re.DOTALL)
            if m and "Array" not in m.group(2):
                free_vars.append(m.group(1))
        elif form.startswith("(assert"):
            assertions.append(form)
        elif form.startswith("(minimize") or form.startswith("(maximize"):
            optimize = form

    if trivial is not None and not assertions:
        return ([], [], [], emitted_logic), trivial, None
    return (declarations, assertions, free_vars, emitted_logic), None, optimize


def solver_cmd(solver_name: str, binary: str) -> list[str]:
    """Return the command line invocation for a given solver reading SMT-LIB from stdin."""
    if solver_name == "z3":
        return [binary, "-in"]
    if solver_name == "cvc5":
        return [binary, "--lang", "smt2", "--produce-models", "-"]
    raise ValueError(f"unknown SMT solver: {solver_name}")


def run_smt_solver(smt2: str, solver_name: str, binary: str, timeout: int) -> str:
    """Send SMT-LIB to the chosen solver via stdin, return its stdout."""
    result = subprocess.run(
        solver_cmd(solver_name, binary),
        input=smt2,
        capture_output=True,
        text=True,
        timeout=timeout,
    )
    return result.stdout


def smt_value_to_decimal(value: str) -> str:
    """Convert an SMT-LIB value token to a decimal string for easier comparison.
    #x03 -> "3", #b101 -> "5", true -> "true", (_ bv3 8) -> "3",
    5 -> "5", (- 5) -> "-5" (LIA)."""
    if value.startswith("#x"):
        return str(int(value[2:], 16))
    if value.startswith("#b"):
        return str(int(value[2:], 2))
    m = re.match(r"\(_ bv(\d+) \d+\)", value)
    if m:
        return m.group(1)
    # LIA: negative integers are emitted as (- N)
    m = re.match(r"\(-\s*(\d+)\)", value)
    if m:
        return "-" + m.group(1)
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

    value_pat = (
        r"(?:#[xb][0-9a-fA-F]+)"
        r"|true|false"
        r"|\(_ bv\d+ \d+\)"
        r"|\(-\s*\d+\)"
        r"|-?\d+"
    )
    pairs = re.findall(
        r"\(\s*(\|[^|]+\||[^()\s|]+)\s+(" + value_pat + r")\s*\)",
        out,
    )
    return {(name[1:-1] if name.startswith("|") else name): value
            for name, value in pairs}


def count_smt(
    file_path: Path,
    length: int,
    ursa: str,
    solver_name: str,
    solver_binary: str,
    max_solutions: int,
    timeout: int,
    total_timeout: float = 60.0,
    single_solution: bool = False,
    logic: str = "QF_BV",
) -> tuple[int, list[dict[str, str]], dict]:
    t_start = time.perf_counter()
    t0 = time.perf_counter()
    (declarations, assertions, free_vars, emitted_logic), trivial, optimize = extract_smt2(
        file_path, length, ursa, logic
    )
    ursa_emit = time.perf_counter() - t0
    timings = {"ursa_emit": ursa_emit, "z3_total": 0.0, "z3_first": None, "z3_calls": 0}

    if trivial is True:
        timings["wall"] = time.perf_counter() - t_start
        return 1, [{}], timings
    if trivial is False:
        timings["wall"] = time.perf_counter() - t_start
        return 0, [], timings

    if optimize is not None and solver_name != "z3":
        print(f"  (skipped: {solver_name} does not support {optimize.split()[0][1:]})")
        timings["wall"] = time.perf_counter() - t_start
        return -1, [], timings

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
        out = run_smt_solver(smt2, solver_name, solver_binary, timeout)
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

    set_logic_line = f"(set-logic {emitted_logic})\n"

    if not free_vars:
        smt2 = set_logic_line + "\n".join(assertions) + "\n(check-sat)\n"
        t0 = time.perf_counter()
        out = run_smt_solver(smt2, solver_name, solver_binary, timeout)
        elapsed = time.perf_counter() - t0
        timings["z3_total"] = elapsed
        timings["z3_first"] = elapsed
        timings["z3_calls"] = 1
        timings["wall"] = time.perf_counter() - t_start
        return (1, [{}], timings) if "sat" in out.split() else (0, [], timings)

    base = (
        set_logic_line
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
        out = run_smt_solver(smt2, solver_name, solver_binary, timeout)
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

        eqs = " ".join(f"(= {n} {v})" for n, v in raw_model.items())
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


def _count_smt_api_z3(
    file_path: Path,
    length: int,
    ursa: str,
    max_solutions: int,
    total_timeout: float = 60.0,
    single_solution: bool = False,
    logic: str = "QF_BV",
) -> tuple[int, list[dict[str, str]], dict]:
    t_start = time.perf_counter()
    t0 = time.perf_counter()
    (declarations, assertions, free_vars, emitted_logic), trivial, optimize = extract_smt2(
        file_path, length, ursa, logic
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
        f"(set-logic {emitted_logic})\n"
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
        for line in msg.splitlines()[:5]:
            print(f"  {line}", file=sys.stderr)
        timings["wall"] = time.perf_counter() - t_start
        return -1, [], timings
    for a in parsed:
        solver.add(a)

    var_consts = {}
    free_var_set = set(free_vars)
    for d in solver.assertions():
        for sub in _collect_free_vars(d):
            name = sub.decl().name()
            if name in free_var_set:
                var_consts.setdefault(name, sub)

    models: list[dict[str, str]] = []
    if not var_consts:
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


def _count_smt_api_cvc5(
    file_path: Path,
    length: int,
    ursa: str,
    max_solutions: int,
    total_timeout: float = 60.0,
    single_solution: bool = False,
    logic: str = "QF_BV",
) -> tuple[int, list[dict[str, str]], dict]:
    if not HAVE_CVC5PY:
        return -1, [], {"error": "cvc5 Python module not installed"}

    t_start = time.perf_counter()
    t0 = time.perf_counter()
    (declarations, assertions, free_vars, emitted_logic), trivial, optimize = extract_smt2(
        file_path, length, ursa, logic
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
        print(f"  (skipped: cvc5 does not support {optimize.split()[0][1:]})")
        timings["wall"] = time.perf_counter() - t_start
        return -1, [], timings

    smt2_text = (
        f"(set-logic {emitted_logic})\n"
        + "\n".join(declarations)
        + "\n"
        + "\n".join(assertions)
        + "\n"
    )

    try:
        tm = cvc5py.TermManager()
        solver = cvc5py.Solver(tm)
        solver.setOption("produce-models", "true")
        parser = cvc5py.InputParser(solver)
        parser.setStringInput(cvc5py.InputLanguage.SMT_LIB_2_6, smt2_text, "input")
        sm = parser.getSymbolManager()
        while True:
            cmd = parser.nextCommand()
            if cmd.isNull():
                break
            cmd.invoke(solver, sm)
    except Exception as e:
        print(f"WARNING: cvc5 API setup failed: {e}", file=sys.stderr)
        timings["wall"] = time.perf_counter() - t_start
        return -1, [], timings

    declared = sm.getDeclaredTerms()
    var_terms = {}
    free_var_set = set(free_vars)
    for term in declared:
        try:
            name = str(term)
            if name in free_var_set:
                var_terms[name] = term
        except Exception:
            pass

    models: list[dict[str, str]] = []
    if not var_terms:
        t0 = time.perf_counter()
        result = solver.checkSat()
        elapsed = time.perf_counter() - t0
        timings["z3_total"] = elapsed
        timings["z3_first"] = elapsed
        timings["z3_calls"] = 1
        timings["wall"] = time.perf_counter() - t_start
        return (1, [{}], timings) if result.isSat() else (0, [], timings)

    progress_every = 50
    hit_total_timeout = False
    while len(models) < max_solutions:
        if time.perf_counter() - t_start > total_timeout:
            hit_total_timeout = True
            break
        t0 = time.perf_counter()
        result = solver.checkSat()
        z3_elapsed = time.perf_counter() - t0
        timings["z3_total"] += z3_elapsed
        timings["z3_calls"] += 1
        if timings["z3_first"] is None:
            timings["z3_first"] = z3_elapsed

        if result.isUnsat():
            break
        if not result.isSat():
            print(f"WARNING: cvc5 API returned {result}", file=sys.stderr)
            break

        raw_model = {}
        decimal_model = {}
        for name, term in var_terms.items():
            val = solver.getValue(term)
            raw_model[name] = val
            decimal_model[name] = smt_value_to_decimal(str(val))

        blocking_parts = []
        for name, term in var_terms.items():
            eq = tm.mkTerm(cvc5py.Kind.EQUAL, term, raw_model[name])
            blocking_parts.append(eq)
        if len(blocking_parts) == 1:
            blocking = tm.mkTerm(cvc5py.Kind.NOT, blocking_parts[0])
        else:
            conj = tm.mkTerm(cvc5py.Kind.AND, *blocking_parts)
            blocking = tm.mkTerm(cvc5py.Kind.NOT, conj)
        solver.assertFormula(blocking)
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


def count_smt_api(
    file_path: Path,
    length: int,
    ursa: str,
    solver_name: str,
    solver_binary: str,
    max_solutions: int,
    total_timeout: float = 60.0,
    single_solution: bool = False,
    logic: str = "QF_BV",
) -> tuple[int, list[dict[str, str]], dict]:
    if solver_name == "z3":
        return _count_smt_api_z3(file_path, length, ursa, max_solutions,
                                 total_timeout, single_solution, logic)
    if solver_name == "cvc5":
        return _count_smt_api_cvc5(file_path, length, ursa, max_solutions,
                                   total_timeout, single_solution, logic)
    raise ValueError(f"unknown solver: {solver_name}")


def _collect_free_vars(expr):
    """Yield uninterpreted constants (free variables) from a Z3 AST."""
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
    parser.add_argument("--cvc5", default="cvc5", help="path to cvc5 binary")
    parser.add_argument("--smt-solver", choices=["z3", "cvc5", "all"], default="z3",
                        help="which SMT solver to use (default: z3). "
                             "'all' runs each supported solver and compares them.")
    parser.add_argument("--no-api", action="store_true",
                        help="skip the Z3 Python API comparison")
    parser.add_argument("--show-models", action="store_true",
                        help="print every model found (otherwise only counts)")
    parser.add_argument("--smt-logic", choices=["QF_BV", "QF_LIA"], default="QF_BV",
                        help="SMT-LIB logic to use for the SMT paths (default QF_BV)")
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

    src = args.file.read_text()
    has_assert_all = bool(re.search(r"\bassert_all\b", src))
    if not has_assert_all and not args.single_solution:
        args.single_solution = True
        print("Mode:   single-solution (source uses `assert`, not `assert_all`; "
              "SMT enumeration stops after 1)")
    elif args.single_solution:
        print("Mode:   single-solution (assert_all rewritten to assert; SMT loop stops after 1)")
    print(flush=True)

    effective_file = maybe_rewrite_single(args.file, args.single_solution and has_assert_all)

    print("Running SAT path...")
    sat_count, sat_models, sat_t = run_sat(effective_file, args.length, args.ursa)
    print(f"  SAT solutions: {sat_count}")
    if args.show_models:
        dump_models(sat_models)

    solver_names = ["z3", "cvc5"] if args.smt_solver == "all" else [args.smt_solver]
    solver_binaries = {"z3": args.z3, "cvc5": args.cvc5}
    api_available_map = {"z3": HAVE_Z3PY, "cvc5": HAVE_CVC5PY}
    api_hint_map = {"z3": "pip install z3-solver", "cvc5": "pip install cvc5"}

    smt_results = {}
    api_results = {}
    for solver_name in solver_names:
        binary = solver_binaries[solver_name]
        print(f"Running SMT path ({args.smt_logic}, subprocess: {solver_name} binary)...", flush=True)
        smt_count, smt_models, smt_t = count_smt(
            effective_file, args.length, args.ursa, solver_name, binary,
            args.max, args.timeout, args.total_timeout, args.single_solution, args.smt_logic,
        )
        print(f"  SMT solutions: {'skipped' if smt_count == -1 else smt_count}")
        if args.show_models:
            dump_models(smt_models)
        smt_results[solver_name] = (smt_count, smt_models, smt_t)

        if api_available_map[solver_name] and not args.no_api:
            print(f"Running SMT path ({args.smt_logic}, {solver_name} Python API)...", flush=True)
            api_count, api_models, api_t = count_smt_api(
                effective_file, args.length, args.ursa, solver_name, binary,
                args.max, args.total_timeout,
                args.single_solution, args.smt_logic,
            )
            print(f"  SMT solutions: {'skipped' if api_count == -1 else api_count}")
            if args.show_models:
                dump_models(api_models)
            api_results[solver_name] = (api_count, api_models, api_t)
        elif not api_available_map[solver_name]:
            print(f"({solver_name} Python API not available; {api_hint_map[solver_name]})")

    print()
    print("Timing:")
    print(f"  SAT             wall: {sat_t['wall']*1000:9.2f} ms")
    if "generation" in sat_t:
        print(f"                  gen:  {sat_t['generation']*1000:9.2f} ms")
    if "solving" in sat_t:
        print(f"                  solv: {sat_t['solving']*1000:9.2f} ms")

    for solver_name in solver_names:
        smt_count, smt_models, smt_t = smt_results[solver_name]
        print(f"  SMT subprocess ({solver_name})  wall: {smt_t['wall']*1000:9.2f} ms")
        print(f"                  emit: {smt_t['ursa_emit']*1000:9.2f} ms")
        print(f"                  {solver_name:5}:{smt_t['z3_total']*1000:9.2f} ms over {smt_t['z3_calls']} {plural(smt_t['z3_calls'], 'call')}")
        if smt_t["z3_first"] is not None:
            print(f"                  1st:  {smt_t['z3_first']*1000:9.2f} ms (first solution)")
        if solver_name in api_results:
            _, _, api_t = api_results[solver_name]
            print(f"  SMT Python API ({solver_name})  wall: {api_t['wall']*1000:9.2f} ms")
            print(f"                  emit: {api_t['ursa_emit']*1000:9.2f} ms")
            print(f"                  {solver_name:5}:{api_t['z3_total']*1000:9.2f} ms over {api_t['z3_calls']} {plural(api_t['z3_calls'], 'call')}")
            if api_t["z3_first"] is not None:
                print(f"                  1st:  {api_t['z3_first']*1000:9.2f} ms (first solution)")

    if sat_t.get("solving") is not None:
        print()
        print("Ratios vs SAT solving:")
        for solver_name in solver_names:
            _, _, smt_t = smt_results[solver_name]
            if smt_t["z3_first"] is not None and sat_t["solving"] > 0:
                print(f"  subprocess {solver_name} first / SAT solving: {smt_t['z3_first']/sat_t['solving']:.1f}x")
            if solver_name in api_results:
                _, _, api_t = api_results[solver_name]
                if api_t["z3_first"] is not None and sat_t["solving"] > 0:
                    print(f"  api {solver_name} first / SAT solving:        {api_t['z3_first']/sat_t['solving']:.1f}x")
                if smt_t["z3_first"] is not None and api_t["z3_first"] is not None and api_t["z3_first"] > 0:
                    print(f"  subprocess {solver_name} overhead vs api (first call): {smt_t['z3_first']/api_t['z3_first']:.1f}x")

    print()
    counts = {"SAT": sat_count}
    skipped = []
    for solver_name in solver_names:
        smt_count, _, _ = smt_results[solver_name]
        if smt_count == -1:
            skipped.append(f"SMT subprocess ({solver_name})")
        else:
            counts[f"SMT subprocess ({solver_name})"] = smt_count
        if solver_name in api_results:
            api_count, _, _ = api_results[solver_name]
            if api_count == -1:
                skipped.append(f"SMT API ({solver_name})")
            else:
                counts[f"SMT API ({solver_name})"] = api_count

    if skipped:
        print(f"Skipped paths: {', '.join(skipped)}")

    distinct = set(counts.values())
    if len(distinct) == 1:
        only = next(iter(distinct))
        print(f"MATCH: all paths found {only} {plural(only, 'solution')}")
        for solver_name in solver_names:
            smt_count, smt_models, _ = smt_results[solver_name]
            if smt_count != -1:
                diff = compare_model_sets(sat_models, smt_models)
                if diff:
                    print(f"NOTE: SAT vs SMT subprocess ({solver_name}) assignments differ:")
                    print(diff)
            if solver_name in api_results:
                api_count, api_models, _ = api_results[solver_name]
                if api_count != -1:
                    diff = compare_model_sets(sat_models, api_models)
                    if diff:
                        print(f"NOTE: SAT vs SMT API ({solver_name}) assignments differ:")
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


def _normalize_var_name(name: str) -> str:
    """Convert `n[k]` (SAT-side array notation) to `n_k_` (SMT-side flat name)
    so that models from the two paths can be compared by matching variables."""
    return name.replace("[", "_").replace("]", "_")


def _restrict_to_common_keys(a: list[dict[str, str]], b: list[dict[str, str]]):
    """Restrict both model lists to keys present in both. Handles the case
    where SMT emits the optimization variable (e.g. nL) but SAT does not."""
    common = set()
    if a:
        common = {_normalize_var_name(k) for k in a[0]}
    if b and common:
        common &= {_normalize_var_name(k) for k in b[0]}
    elif b:
        common = {_normalize_var_name(k) for k in b[0]}
    return (
        [{_normalize_var_name(k): v for k, v in m.items() if _normalize_var_name(k) in common} for m in a],
        [{_normalize_var_name(k): v for k, v in m.items() if _normalize_var_name(k) in common} for m in b],
    )


def compare_model_sets(a: list[dict[str, str]], b: list[dict[str, str]]) -> str:
    """If two model lists differ as sets, return a human-readable diff. Else ''.

    Comparison is done on the intersection of variable names, so a difference in
    which side reports the optimization variable (SMT does, SAT usually does not)
    does not count as a mismatch when the shared variables all agree.
    """
    a_common, b_common = _restrict_to_common_keys(a, b)
    def freeze(m: dict[str, str]) -> frozenset:
        return frozenset(m.items())
    sa = {freeze(m) for m in a_common}
    sb = {freeze(m) for m in b_common}
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
