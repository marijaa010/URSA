#include "SMTSolverDriver.hpp"

#ifdef Z3_SUPPORT
#include <z3++.h>
#endif
#ifdef CVC5_SUPPORT
#include <cvc5/cvc5.h>
#include <cvc5/cvc5_parser.h>
#endif

#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <set>
#include <utility>
#include <regex>
#include <chrono>
#include <cctype>
#include <cstdlib>

using namespace std;

namespace {

using clock_t_ = std::chrono::steady_clock;

// Helpers shared by the Z3 and cvc5 backends; compiled only when at least
// one solver is linked in (otherwise they would be unused).
#if defined(Z3_SUPPORT) || defined(CVC5_SUPPORT)

string unflattenArrayName(const string& name) {
    static const regex r2d(R"(^(.+?)_(\d+)__(\d+)_$)");
    static const regex r1d(R"(^(.+?)_(\d+)_$)");
    smatch m;
    if (regex_match(name, m, r2d))
        return m[1].str() + "[" + m[2].str() + "][" + m[3].str() + "]";
    if (regex_match(name, m, r1d))
        return m[1].str() + "[" + m[2].str() + "]";
    return name;
}

void printSolution(bool assertAll, int solutionNumber,
                   const vector<pair<string, string>>& values) {
    if (assertAll) cout << "--> Solution " << solutionNumber << endl;
    for (const auto& kv : values)
        cout << unflattenArrayName(kv.first) << "=" << kv.second << ";" << endl;
    if (assertAll) cout << endl;
}

string extractSetLogic(const string& buffer) {
    size_t p = buffer.find("(set-logic");
    if (p == string::npos) return "";
    p = buffer.find_first_not_of(" \t", p + 10);
    if (p == string::npos) return "";
    size_t e = buffer.find_first_of(" \t)", p);
    if (e == string::npos) return "";
    return buffer.substr(p, e - p);
}

void printTail(int solutionCount, bool assertAll, int checkSatCalls,
               const char* tag, const string& logic, double totalSeconds) {
    if (solutionCount == 0) {
        cout << "No solutions found." << endl;
    } else if (assertAll) {
        cerr << "[Number of solutions: " << solutionCount << "]" << endl;
    }
    cerr << "[SMT solving (" << tag;
    if (!logic.empty()) cerr << ", " << logic;
    cerr << "): over " << checkSatCalls
         << " check-sat " << (checkSatCalls == 1 ? "call" : "calls")
         << ", total: " << totalSeconds << "s]" << endl;
}

uint64_t parseSmtLiteral(const string& tok) {
    if (tok.size() > 2 && tok[0] == '#' && (tok[1] == 'x' || tok[1] == 'X'))
        return strtoull(tok.c_str() + 2, nullptr, 16);
    if (tok.size() > 2 && tok[0] == '#' && (tok[1] == 'b' || tok[1] == 'B'))
        return strtoull(tok.c_str() + 2, nullptr, 2);
    return strtoull(tok.c_str(), nullptr, 10);
}

vector<uint64_t> accessedArrayIndices(const string& buffer,
                                      const string& arrName) {
    vector<uint64_t> out;
    set<uint64_t> seen;
    const string needle = "(select " + arrName + " ";
    size_t pos = 0;
    while ((pos = buffer.find(needle, pos)) != string::npos) {
        size_t p = pos + needle.size();
        size_t e = p;
        while (e < buffer.size() && buffer[e] != ' ' && buffer[e] != ')') e++;
        string tok = buffer.substr(p, e - p);
        pos = e;
        if (tok.empty()) continue;
        bool literal = (tok[0] == '#') || isdigit((unsigned char)tok[0]);
        if (!literal) continue;
        uint64_t v = parseSmtLiteral(tok);
        if (seen.insert(v).second) out.push_back(v);
    }
    return out;
}

#endif  // Z3_SUPPORT || CVC5_SUPPORT

bool isTrivial(const string& buffer) {
    return buffer.find("yes (trivially)") != string::npos ||
           buffer.find("no (trivially)") != string::npos;
}

// ---------------------------------------------------------------------------
// Z3 backend
// ---------------------------------------------------------------------------

#ifdef Z3_SUPPORT

struct FreeVar {
    string name;
    string sort;
};

vector<FreeVar> extractFreeVars(const string& buffer) {
    vector<FreeVar> result;
    istringstream iss(buffer);
    string line;
    while (getline(iss, line)) {
        if (line.compare(0, 13, "(declare-fun ") != 0) continue;
        size_t nameStart = 13;
        size_t nameEnd = line.find(' ', nameStart);
        if (nameEnd == string::npos) continue;
        string name = line.substr(nameStart, nameEnd - nameStart);
        size_t sortStart = line.find("()", nameEnd);
        if (sortStart == string::npos) continue;
        sortStart += 2;
        while (sortStart < line.size() && line[sortStart] == ' ') sortStart++;
        size_t sortEnd = line.size();
        while (sortEnd > sortStart && line[sortEnd - 1] != ')') sortEnd--;
        if (sortEnd == sortStart) continue;
        string sort = line.substr(sortStart, sortEnd - sortStart - 1);
        if (sort.find("Array") != string::npos) continue;
        result.push_back(FreeVar{name, sort});
    }
    return result;
}

struct ArrayFree {
    string name;
    bool intIndex; unsigned indexWidth;
    bool intElem;  unsigned elemWidth;
};

vector<ArrayFree> extractArrayVars(const string& buffer) {
    vector<ArrayFree> result;
    static const regex bvArr(
        R"(\(declare-fun ([^ ]+) \(\) \(Array \(_ BitVec (\d+)\) \(_ BitVec (\d+)\)\)\))");
    static const regex intArr(
        R"(\(declare-fun ([^ ]+) \(\) \(Array Int Int\)\))");
    istringstream iss(buffer);
    string line;
    while (getline(iss, line)) {
        smatch m;
        if (regex_search(line, m, bvArr)) {
            result.push_back(ArrayFree{m[1].str(), false,
                                       (unsigned)stoul(m[2].str()), false,
                                       (unsigned)stoul(m[3].str())});
        } else if (regex_search(line, m, intArr)) {
            result.push_back(ArrayFree{m[1].str(), true, 0, true, 0});
        }
    }
    return result;
}

string stripQuotes(const string& s) {
    if (s.size() >= 2 && s.front() == '|' && s.back() == '|')
        return s.substr(1, s.size() - 2);
    return s;
}

z3::expr makeZ3Const(z3::context& ctx, const FreeVar& fv) {
    string n = stripQuotes(fv.name);
    if (fv.sort == "Int")  return ctx.int_const(n.c_str());
    if (fv.sort == "Bool") return ctx.bool_const(n.c_str());
    unsigned w = 8;
    size_t p = fv.sort.find("BitVec");
    if (p != string::npos) {
        size_t q = p + 6;
        while (q < fv.sort.size() && !isdigit((unsigned char)fv.sort[q])) q++;
        unsigned parsed = (unsigned)strtoul(fv.sort.c_str() + q, nullptr, 10);
        if (parsed > 0) w = parsed;
    }
    return ctx.bv_const(n.c_str(), w);
}

string z3ValueToDecimal(const z3::expr& v) {
    if (v.is_bool()) return v.is_true() ? "true" : "false";
    return Z3_get_numeral_string(v.ctx(), v);
}

int runZ3(const string& buffer, bool assertAll, bool hasOptimize) {
    vector<FreeVar> freeVars = extractFreeVars(buffer);
    auto start = clock_t_::now();
    int solutionCount = 0;
    int checkSatCalls = 0;
    try {
        z3::context ctx;
        vector<z3::expr> consts;
        consts.reserve(freeVars.size());
        for (const auto& fv : freeVars) consts.push_back(makeZ3Const(ctx, fv));

        if (hasOptimize) {
            z3::optimize opt(ctx);
            opt.from_string(buffer.c_str());
            checkSatCalls++;
            if (opt.check() == z3::sat) {
                z3::model m = opt.get_model();
                vector<pair<string, string>> values;
                for (size_t k = 0; k < consts.size(); k++)
                    values.push_back({freeVars[k].name,
                                      z3ValueToDecimal(m.eval(consts[k], true))});
                solutionCount++;
                printSolution(assertAll, solutionCount, values);
            }
        } else {
            z3::solver s(ctx);
            s.from_string(buffer.c_str());

            set<string> scalarNames;
            for (const auto& fv : freeVars) scalarNames.insert(fv.name);
            struct CellExpr { z3::expr sel; string display; bool mirrored; };
            vector<CellExpr> cells;
            for (const auto& av : extractArrayVars(buffer)) {
                z3::sort idxSort  = av.intIndex ? ctx.int_sort()
                                                : ctx.bv_sort(av.indexWidth);
                z3::sort elemSort = av.intElem  ? ctx.int_sort()
                                                : ctx.bv_sort(av.elemWidth);
                z3::expr arr = ctx.constant(
                    av.name.c_str(), ctx.array_sort(idxSort, elemSort));
                for (uint64_t idx : accessedArrayIndices(buffer, av.name)) {
                    z3::expr iexpr = av.intIndex
                        ? ctx.int_val((int)idx)
                        : ctx.bv_val((int)idx, av.indexWidth);
                    string mirror = av.name + "_" + std::to_string(idx) + "_";
                    string disp   = av.name + "[" + std::to_string(idx) + "]";
                    cells.push_back({z3::select(arr, iexpr), disp,
                                     scalarNames.count(mirror) > 0});
                }
            }

            while (true) {
                checkSatCalls++;
                z3::check_result r = s.check();
                if (r == z3::unsat) break;
                if (r == z3::unknown) {
                    cerr << "Solver returned 'unknown' - could not determine "
                            "satisfiability." << endl;
                    break;
                }
                z3::model m = s.get_model();
                vector<pair<string, string>> values;
                z3::expr_vector eqs(ctx);
                for (size_t k = 0; k < consts.size(); k++) {
                    z3::expr val = m.eval(consts[k], true);
                    values.push_back({freeVars[k].name, z3ValueToDecimal(val)});
                    eqs.push_back(consts[k] == val);
                }
                for (const auto& c : cells) {
                    z3::expr val = m.eval(c.sel, true);
                    if (!c.mirrored)
                        values.push_back({c.display, z3ValueToDecimal(val)});
                    eqs.push_back(c.sel == val);
                }
                solutionCount++;
                printSolution(assertAll, solutionCount, values);
                if (!assertAll || eqs.empty()) break;
                s.add(!z3::mk_and(eqs));
            }
        }
    } catch (const z3::exception& e) {
        cerr << "ERROR: Z3 failure: " << e.msg() << endl;
        return 1;
    }
    double total = std::chrono::duration<double>(clock_t_::now() - start).count();
    printTail(solutionCount, assertAll, checkSatCalls, "z3", extractSetLogic(buffer), total);
    return 0;
}

#endif  // Z3_SUPPORT

// ---------------------------------------------------------------------------
// cvc5 backend
// ---------------------------------------------------------------------------

#ifdef CVC5_SUPPORT

string cvc5ValueToDecimal(const cvc5::Term& v) {
    cvc5::Sort s = v.getSort();
    if (s.isBitVector()) return v.getBitVectorValue(10);
    if (s.isInteger())   return v.getIntegerValue();
    if (s.isBoolean())   return v.getBooleanValue() ? "true" : "false";
    return v.toString();
}

int runCVC5(const string& buffer, bool assertAll, bool hasOptimize) {
    if (hasOptimize) {
        cerr << "ERROR: solver 'cvc5' does not support (minimize)/(maximize)."
             << endl
             << "       Use -smtsolve=z3 for programs with minimize/maximize,"
             << endl
             << "       or -smt to emit SMT-LIB and drive a solver manually."
             << endl;
        return 1;
    }
    auto start = clock_t_::now();
    int solutionCount = 0;
    int checkSatCalls = 0;
    try {
        cvc5::TermManager tm;
        cvc5::Solver slv(tm);
        slv.setOption("produce-models", "true");
        slv.setOption("incremental", "true");

        cvc5::parser::SymbolManager sm(tm);
        cvc5::parser::InputParser parser(&slv, &sm);
        parser.setStringInput(cvc5::modes::InputLanguage::SMT_LIB_2_6, buffer,
                              "ursa");
        while (true) {
            cvc5::parser::Command cmd = parser.nextCommand();
            if (cmd.isNull()) break;
            cmd.invoke(&slv, &sm, cout);
        }

        vector<cvc5::Term> freeVars;
        vector<cvc5::Term> arrayTerms;
        set<string> scalarNames;
        for (const cvc5::Term& t : sm.getDeclaredTerms()) {
            if (t.getSort().isArray()) arrayTerms.push_back(t);
            else { freeVars.push_back(t); scalarNames.insert(t.getSymbol()); }
        }

        struct CellTerm { cvc5::Term sel; string display; bool mirrored; };
        vector<CellTerm> cells;
        for (const cvc5::Term& arr : arrayTerms) {
            cvc5::Sort idxSort = arr.getSort().getArrayIndexSort();
            for (uint64_t idx : accessedArrayIndices(buffer, arr.getSymbol())) {
                cvc5::Term iterm = idxSort.isBitVector()
                    ? tm.mkBitVector(idxSort.getBitVectorSize(), idx)
                    : tm.mkInteger((int64_t)idx);
                cvc5::Term sel = tm.mkTerm(cvc5::Kind::SELECT, {arr, iterm});
                string mirror = arr.getSymbol() + "_" + std::to_string(idx) + "_";
                string disp   = arr.getSymbol() + "[" + std::to_string(idx) + "]";
                cells.push_back({sel, disp, scalarNames.count(mirror) > 0});
            }
        }

        while (true) {
            checkSatCalls++;
            if (!slv.checkSat().isSat()) break;
            vector<pair<string, string>> values;
            vector<cvc5::Term> eqs;
            for (const cvc5::Term& t : freeVars) {
                cvc5::Term v = slv.getValue(t);
                values.push_back({t.getSymbol(), cvc5ValueToDecimal(v)});
                eqs.push_back(tm.mkTerm(cvc5::Kind::EQUAL, {t, v}));
            }
            for (const auto& c : cells) {
                cvc5::Term v = slv.getValue(c.sel);
                if (!c.mirrored)
                    values.push_back({c.display, cvc5ValueToDecimal(v)});
                eqs.push_back(tm.mkTerm(cvc5::Kind::EQUAL, {c.sel, v}));
            }
            solutionCount++;
            printSolution(assertAll, solutionCount, values);
            if (!assertAll || eqs.empty()) break;
            cvc5::Term conj =
                (eqs.size() == 1) ? eqs[0] : tm.mkTerm(cvc5::Kind::AND, eqs);
            slv.assertFormula(tm.mkTerm(cvc5::Kind::NOT, {conj}));
        }
    } catch (const cvc5::parser::ParserException& e) {
        cerr << "ERROR: cvc5 parser failure: " << e.getMessage() << endl;
        return 1;
    } catch (const cvc5::CVC5ApiException& e) {
        cerr << "ERROR: cvc5 failure: " << e.getMessage() << endl;
        return 1;
    }
    double total = std::chrono::duration<double>(clock_t_::now() - start).count();
    printTail(solutionCount, assertAll, checkSatCalls, "cvc5", extractSetLogic(buffer), total);
    return 0;
}

#endif  // CVC5_SUPPORT

}  // namespace

int SMTSolverDriver::run() {
    if (isTrivial(m_buffer)) {
        cout << m_buffer;
        return 0;
    }
    if (m_solver == eSolverZ3) {
#ifdef Z3_SUPPORT
        return runZ3(m_buffer, m_assertAll, m_hasOptimize);
#else
        cerr << "ERROR: this URSA build has no Z3 support "
                "(rebuild with Z3_SUPPORT=1)." << endl;
        return 1;
#endif
    }
#ifdef CVC5_SUPPORT
    return runCVC5(m_buffer, m_assertAll, m_hasOptimize);
#else
    cerr << "ERROR: this URSA build has no cvc5 support "
            "(rebuild with CVC5_SUPPORT=1)." << endl;
    return 1;
#endif
}
