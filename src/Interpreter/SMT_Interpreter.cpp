#include <iostream>
#include <fstream>
#include <sstream>
#include <cassert>
#include "SMT_Interpreter.hpp"
#include "ursa.tab.hpp"

using namespace std;

extern map<const string, nodeType *, lstr> URSAprocedures;
extern unsigned int iAbstractNumberLength;
extern bool bQuiet;


static uint64_t parseIntLiteral(const char* s) {
    if (!s) return 0;
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        uint64_t v = 0;
        for (const char* p = s + 2; *p; p++) {
            char c = *p;
            int d = (c >= '0' && c <= '9') ? c - '0'
                  : (c >= 'a' && c <= 'f') ? c - 'a' + 10
                  : (c >= 'A' && c <= 'F') ? c - 'A' + 10 : -1;
            if (d < 0) break;
            v = (v << 4) | (uint64_t)d;
        }
        return v;
    }
    if (s[0] == '0' && (s[1] == 'b' || s[1] == 'B')) {
        uint64_t v = 0;
        for (const char* p = s + 2; *p; p++) {
            if (*p != '0' && *p != '1') break;
            v = (v << 1) | (uint64_t)(*p - '0');
        }
        return v;
    }
    uint64_t v = 0;
    for (const char* p = s; *p; p++) {
        if (*p < '0' || *p > '9') break;
        v = v * 10 + (uint64_t)(*p - '0');
    }
    return v;
}


bool SMTInterpreter::IsNumberId(nodeType *p) {
    return ((p->type == typeId) && (*(p->id.i) == 'n')) ||
           ((p->opr.oper == '@') && (*(p->opr.op[0]->id.i) == 'n'));
}

bool SMTInterpreter::IsBooleanId(nodeType *p) {
    return ((p->type == typeId) && (*(p->id.i) == 'b')) ||
           ((p->opr.oper == '@') && (*(p->opr.op[0]->id.i) == 'b'));
}


int SMTInterpreter::ExecuteCommand(nodeType *p) {
    if (!p) return -1;
    if (p->type != typeOpr) return -1;

    switch (p->opr.oper) {
      case CALL:    ExecuteProcedure(p); return 0;

      case LIST:    return 0;
      case CLEAR:   m_ST.Clear(); ClearProgram(); return 0;

      case HALT:    m_ST.Clear(); ClearProgram();
                    cout << "--> Ending session" << endl << endl;
                    exit(0);

      case FOR:     ExecuteCommand(p->opr.op[0]);
                    while (ReadBoolean(p->opr.op[1]).GetGroundValue()) {
                        ExecuteCommand(p->opr.op[3]);
                        ExecuteCommand(p->opr.op[2]);
                    }
                    return 0;

      case WHILE:   while (ReadBoolean(p->opr.op[0]).GetGroundValue())
                        ExecuteCommand(p->opr.op[1]);
                    return 0;

      case IF:      if (ReadBoolean(p->opr.op[0]).GetGroundValue())
                        return ExecuteCommand(p->opr.op[1]);
                    else {
                        if (p->opr.nops > 2) return ExecuteCommand(p->opr.op[2]);
                        return 0;
                    }

      case MINIMIZE:
      case MAXIMIZE: {
                    if (m_hasOptimization) {
                        cout << "Only the first optimization constraint is considered" << endl;
                        return 0;
                    }
                    SMTNumber minVal = ReadNumber(p->opr.op[1]);
                    SMTNumber maxVal = ReadNumber(p->opr.op[2]);
                    if (!minVal.IsGroundNumber() || !maxVal.IsGroundNumber()) {
                        cout << "[SMT mode] minimize/maximize bounds must be ground" << endl;
                        return 0;
                    }
                    m_hasOptimization = true;
                    m_optVarName = p->opr.op[0]->id.i;
                    m_optMin = minVal.GetGroundValueUnsigned();
                    m_optMax = maxVal.GetGroundValueUnsigned();
                    m_optMaximize = (p->opr.oper == MAXIMIZE);
                    return 0;
      }

      case ASSERT:  Solve(p->opr.op[0], false); return 0;
      case ASSERTA: Solve(p->opr.op[0], true);  return 0;

      case PRINT:
      case PRINTX:
      case PRINTB:  return 0;

      case ';':     ExecuteCommand(p->opr.op[0]);
                    return ExecuteCommand(p->opr.op[1]);

      case '=':
                    if (p->opr.nops == 2) {
                        if (IsNumberId(p->opr.op[0]))
                            m_ST.letInt(p->opr.op[0]->id.i, ReadNumber(p->opr.op[1]));
                        else
                            m_ST.letBool(p->opr.op[0]->id.i, ReadBoolean(p->opr.op[1]));
                    }
                    else if (p->opr.nops == 3) {
                        if (IsNumberId(p->opr.op[0]))
                            m_ST.letIntEl(p->opr.op[0]->id.i, ReadNumber(p->opr.op[1]), ReadNumber(p->opr.op[2]));
                        else
                            m_ST.letBoolEl(p->opr.op[0]->id.i, ReadNumber(p->opr.op[1]), ReadBoolean(p->opr.op[2]));
                    }
                    else {
                        if (IsNumberId(p->opr.op[0]))
                            m_ST.letIntEl2(p->opr.op[0]->id.i, ReadNumber(p->opr.op[1]),
                                            ReadNumber(p->opr.op[2]), ReadNumber(p->opr.op[3]));
                        else
                            m_ST.letBoolEl2(p->opr.op[0]->id.i, ReadNumber(p->opr.op[1]),
                                             ReadNumber(p->opr.op[2]), ReadBoolean(p->opr.op[3]));
                    }
                    return 0;

      case PLUSPLUS:
      case MINUSMINUS:
      {
          SMTNumber nleft;
          if      (p->opr.nops == 1) nleft = m_ST.getIntValue(p->opr.op[0]->id.i);
          else if (p->opr.nops == 2) nleft = m_ST.getIntElValue(p->opr.op[0]->id.i, ReadNumber(p->opr.op[1]));
          else                       nleft = m_ST.getIntElValue2(p->opr.op[0]->id.i, ReadNumber(p->opr.op[1]), ReadNumber(p->opr.op[2]));
          SMTNumber one((uint64_t)1, (int)iAbstractNumberLength);
          SMTNumber n = (p->opr.oper == PLUSPLUS) ? (nleft + one) : (nleft - one);
          if      (p->opr.nops == 1) m_ST.letInt(p->opr.op[0]->id.i, n);
          else if (p->opr.nops == 2) m_ST.letIntEl(p->opr.op[0]->id.i, ReadNumber(p->opr.op[1]), n);
          else                       m_ST.letIntEl2(p->opr.op[0]->id.i, ReadNumber(p->opr.op[1]), ReadNumber(p->opr.op[2]), n);
          return 0;
      }

      case PLUSEQ:
      case MINUSEQ:
      case MULTEQ:
      case BITWISEANDEQ:
      case BITWISEOREQ:
      case BITWISEXOREQ:
      case LSHIFTEQ:
      case RSHIFTEQ:
      {
          SMTNumber nleft;
          if      (p->opr.nops == 2) nleft = m_ST.getIntValue(p->opr.op[0]->id.i);
          else if (p->opr.nops == 3) nleft = m_ST.getIntElValue(p->opr.op[0]->id.i, ReadNumber(p->opr.op[1]));
          else                       nleft = m_ST.getIntElValue2(p->opr.op[0]->id.i, ReadNumber(p->opr.op[1]), ReadNumber(p->opr.op[2]));

          SMTNumber rhs = ReadNumber(p->opr.op[p->opr.nops - 1]);
          SMTNumber n;
          switch (p->opr.oper) {
            case PLUSEQ:        n = nleft + rhs; break;
            case MINUSEQ:       n = nleft - rhs; break;
            case MULTEQ:        n = nleft * rhs; break;
            case BITWISEANDEQ:  n = nleft & rhs; break;
            case BITWISEOREQ:   n = nleft | rhs; break;
            case BITWISEXOREQ:  n = nleft ^ rhs; break;
            case LSHIFTEQ:      n = nleft << rhs; break;
            case RSHIFTEQ:      n = nleft >> rhs; break;
          }
          if      (p->opr.nops == 2) m_ST.letInt(p->opr.op[0]->id.i, n);
          else if (p->opr.nops == 3) m_ST.letIntEl(p->opr.op[0]->id.i, ReadNumber(p->opr.op[1]), n);
          else                       m_ST.letIntEl2(p->opr.op[0]->id.i, ReadNumber(p->opr.op[1]), ReadNumber(p->opr.op[2]), n);
          return 0;
      }

      case ANDEQ:
      case OREQ:
      case XOREQ:
      {
          SMTBoolean bleft;
          if      (p->opr.nops == 2) bleft = m_ST.getBoolValue(p->opr.op[0]->id.i);
          else if (p->opr.nops == 3) bleft = m_ST.getBoolElValue(p->opr.op[0]->id.i, ReadNumber(p->opr.op[1]));
          else                       bleft = m_ST.getBoolElValue2(p->opr.op[0]->id.i, ReadNumber(p->opr.op[1]), ReadNumber(p->opr.op[2]));

          SMTBoolean rhs = ReadBoolean(p->opr.op[p->opr.nops - 1]);
          SMTBoolean b;
          switch (p->opr.oper) {
            case ANDEQ: b = bleft & rhs; break;
            case OREQ:  b = bleft | rhs; break;
            case XOREQ: b = bleft ^ rhs; break;
          }
          if      (p->opr.nops == 2) m_ST.letBool(p->opr.op[0]->id.i, b);
          else if (p->opr.nops == 3) m_ST.letBoolEl(p->opr.op[0]->id.i, ReadNumber(p->opr.op[1]), b);
          else                       m_ST.letBoolEl2(p->opr.op[0]->id.i, ReadNumber(p->opr.op[1]), ReadNumber(p->opr.op[2]), b);
          return 0;
      }

      default: break;
    }
    return 0;
}




SMTNumber SMTInterpreter::ReadNumber(nodeType *p) {
    if (!p) return SMTNumber((uint64_t)0, (int)iAbstractNumberLength);
    switch (p->type) {
      case typeIntConst:
        return SMTNumber(parseIntLiteral(p->intConst.value), (int)iAbstractNumberLength);
      case typeId:
        return m_ST.getIntValue(p->id.i);
      case typeOpr:
        switch (p->opr.oper) {
          case '@':
            if (p->opr.nops == 2)
                return m_ST.getIntElValue(p->opr.op[0]->id.i, ReadNumber(p->opr.op[1]));
            else
                return m_ST.getIntElValue2(p->opr.op[0]->id.i, ReadNumber(p->opr.op[1]), ReadNumber(p->opr.op[2]));
          case UMINUS:   return ReadNumber(p->opr.op[0]).negate();
          case '+':      return ReadNumber(p->opr.op[0]) + ReadNumber(p->opr.op[1]);
          case '-':      return ReadNumber(p->opr.op[0]) - ReadNumber(p->opr.op[1]);
          case '*':      return ReadNumber(p->opr.op[0]) * ReadNumber(p->opr.op[1]);
          case '&':      return ReadNumber(p->opr.op[0]) & ReadNumber(p->opr.op[1]);
          case '|':      return ReadNumber(p->opr.op[0]) | ReadNumber(p->opr.op[1]);
          case '^':      return ReadNumber(p->opr.op[0]) ^ ReadNumber(p->opr.op[1]);
          case '~':      return ReadNumber(p->opr.op[0]).bitnegate();
          case LSHIFT:   return ReadNumber(p->opr.op[0]) << ReadNumber(p->opr.op[1]);
          case RSHIFT:   return ReadNumber(p->opr.op[0]) >> ReadNumber(p->opr.op[1]);
          case ITE:      return ReadNumber(p->opr.op[1]).ite(ReadBoolean(p->opr.op[0]), ReadNumber(p->opr.op[2]));
          case BOOL2NUM: return ReadBoolean(p->opr.op[0]).Int();
          case SGN:      return ReadNumber(p->opr.op[0]).sgn();
        }
      default: break;
    }
    return SMTNumber((uint64_t)0, (int)iAbstractNumberLength);
}




SMTBoolean SMTInterpreter::ReadBoolean(nodeType *p) {
    if (!p) return SMTBoolean(false);
    switch (p->type) {
      case typeBoolConst: return SMTBoolean(p->boolConst.value);
      case typeId:        return m_ST.getBoolValue(p->id.i);
      case typeOpr:
        switch (p->opr.oper) {
          case '@':
            if (p->opr.nops == 2)
                return m_ST.getBoolElValue(p->opr.op[0]->id.i, ReadNumber(p->opr.op[1]));
            else
                return m_ST.getBoolElValue2(p->opr.op[0]->id.i, ReadNumber(p->opr.op[1]), ReadNumber(p->opr.op[2]));
          case LOGICALAND: return ReadBoolean(p->opr.op[0]) & ReadBoolean(p->opr.op[1]);
          case LOGICALOR:  return ReadBoolean(p->opr.op[0]) | ReadBoolean(p->opr.op[1]);
          case LOGICALXOR: return ReadBoolean(p->opr.op[0]) ^ ReadBoolean(p->opr.op[1]);
          case '!':        return ReadBoolean(p->opr.op[0]).negate();
          case '<':        return ReadNumber(p->opr.op[0]) <  ReadNumber(p->opr.op[1]);
          case '>':        return ReadNumber(p->opr.op[0]) >  ReadNumber(p->opr.op[1]);
          case LE:         return ReadNumber(p->opr.op[0]) <= ReadNumber(p->opr.op[1]);
          case GE:         return ReadNumber(p->opr.op[0]) >= ReadNumber(p->opr.op[1]);
          case EQ:         return ReadNumber(p->opr.op[0]) == ReadNumber(p->opr.op[1]);
          case NE:         return ReadNumber(p->opr.op[0]) != ReadNumber(p->opr.op[1]);
          case ITE:        return ReadBoolean(p->opr.op[1]).ite(ReadBoolean(p->opr.op[0]), ReadBoolean(p->opr.op[2]));
          case NUM2BOOL:   return ReadNumber(p->opr.op[0]).Bool();
        }
      default: break;
    }
    return SMTBoolean(false);
}




bool SMTInterpreter::Solve(nodeType *p, bool bAllSolutions) {
    return SolveConstraint(p, bAllSolutions);
}

bool SMTInterpreter::SolveConstraint(nodeType *p, bool /*bAllSolutions*/) {
    double dTime_parsing = m_Timer.ElapsedTime();
    m_Timer.StartMeasuringTime();

    SMTBoolean bConstraint(true);
    bool bMoreConstraints = true;
    while (bMoreConstraints) {
        if (p->opr.oper == ';') {
            bConstraint = bConstraint & ReadBoolean(p->opr.op[1]);
            p = p->opr.op[0];
        } else {
            bConstraint = bConstraint & ReadBoolean(p);
            bMoreConstraints = false;
        }
    }

    if (bConstraint.IsGroundBoolean()) {
        if (bConstraint.GetGroundValue()) {
            cout << "yes (trivially)" << endl;
            return true;
        }
        cout << "no (trivially)" << endl;
        return false;
    }

    m_assertions.push_back(bConstraint);

    if (!m_hasOptimization) {
        cout << "(set-logic QF_BV)" << endl;
    }
    m_ST.collectFreeVarDeclarations(cout);
    cout << endl;
    for (auto& a : m_assertions) {
        cout << "(assert ";
        // Use the let-binding pretty-printer for top-level assertions:
        // shared subexpressions get named (avoiding repetition in output),
        // conjuncts of a big AND spine appear on separate lines with indent.
        if (a.getExpr()) a.getExpr()->printWithLet(cout, /*indent=*/8);
        cout << ")" << endl;
    }
    if (m_hasOptimization) {
        unsigned int w = iAbstractNumberLength;
        cout << "(assert (bvuge " << m_optVarName << " "
             << SMTExpr::formatBvConst(m_optMin, w) << "))" << endl;
        cout << "(assert (bvule " << m_optVarName << " "
             << SMTExpr::formatBvConst(m_optMax, w) << "))" << endl;
        cout << "(" << (m_optMaximize ? "maximize" : "minimize")
             << " " << m_optVarName << ")" << endl;
    }
    cout << endl;
    cout << "(check-sat)" << endl;
    if (m_hasOptimization) {
        cout << "(get-objectives)" << endl;
    }
    cout << "(get-model)" << endl;

    double dTime_generation = m_Timer.ElapsedTime();
    if (!bQuiet) {
        cerr << "[SMT generation: " << dTime_parsing + dTime_generation << "s]" << endl;

        // Hash-consing statistics: tree-size = nodes counted with multiplicity
        // (what the printer would emit if everything were inlined); cache-size
        // = unique nodes after sharing. Ratio shows how much sharing saves.
        size_t treeTotal = 0;
        for (const auto& a : m_assertions) {
            if (a.getExpr()) treeTotal += a.getExpr()->treeSize();
        }
        size_t unique = SMTFactory::cacheSize();
        if (unique > 0) {
            double ratio = (double)treeTotal / (double)unique;
            cerr << "[Hash-cons: " << treeTotal << " logical nodes, "
                 << unique << " unique (sharing factor " << ratio << "x)]" << endl;
        }
    }
    return true;
}




bool SMTInterpreter::ExecuteProcedure(nodeType *p) {
    auto it = URSAprocedures.find(p->opr.op[0]->id.i);
    if (it == URSAprocedures.end()) {
        cout << "Procedure " << p->opr.op[0]->id.i << " not defined" << endl;
        return false;
    }

    nodeType *pargs = p->opr.op[1];
    nodeType *pdefargs = it->second->opr.op[1];
    SMTInterpreter procInter;

    int actualargs = 0;
    while (pargs->opr.oper == ';') { pargs = pargs->opr.op[0]; actualargs++; }
    int defargs = 0;
    while (pdefargs->opr.oper == ';') { pdefargs = pdefargs->opr.op[0]; defargs++; }
    if (actualargs != defargs) {
        cout << "Wrong number of arguments in the call of the procedure "
             << p->opr.op[0]->id.i << endl;
        exit(1);
    }

    pargs = p->opr.op[1];
    pdefargs = it->second->opr.op[1];
    bool bMoreArgs = true;
    while (bMoreArgs) {
        nodeType *parg, *pdef;
        if (pargs->opr.oper == ';') { parg = pargs->opr.op[1]; pdef = pdefargs->opr.op[1]; }
        else                        { parg = pargs;            pdef = pdefargs; bMoreArgs = false; }

        if (IsNumberId(pdef)) {
            if (!IsNumberId(parg) || m_ST.DefinedIntVar(parg->id.i))
                procInter.m_ST.letInt(pdef->id.i, ReadNumber(parg));
            procInter.m_ST.SetAccessedIntVar(pdef->id.i, false);
        } else {
            if (!IsBooleanId(parg) || m_ST.DefinedBoolVar(parg->id.i))
                procInter.m_ST.letBool(pdef->id.i, ReadBoolean(parg));
            procInter.m_ST.SetAccessedBoolVar(pdef->id.i, false);
        }

        if (bMoreArgs) {
            pargs = pargs->opr.op[0];
            pdefargs = pdefargs->opr.op[0];
        }
    }

    nodeType *pcode = it->second->opr.op[2];
    procInter.ExecuteCommandTree(pcode);

    pargs = p->opr.op[1];
    pdefargs = it->second->opr.op[1];
    bMoreArgs = true;
    while (bMoreArgs) {
        nodeType *parg, *pdef;
        if (pargs->opr.oper == ';') { parg = pargs->opr.op[1]; pdef = pdefargs->opr.op[1]; }
        else                        { parg = pargs;            pdef = pdefargs; bMoreArgs = false; }

        if (IsNumberId(parg)) {
            if (!procInter.m_ST.GetAccessedIntVar(pdef->id.i) && !m_ST.DefinedIntVar(parg->id.i))
                procInter.m_ST.letInt(pdef->id.i, SMTNumber((uint64_t)0, (int)iAbstractNumberLength));
            m_ST.letInt(parg->id.i, procInter.ReadNumber(pdef));
        } else if (IsBooleanId(parg)) {
            if (!procInter.m_ST.GetAccessedBoolVar(pdef->id.i) && !m_ST.DefinedBoolVar(parg->id.i))
                procInter.m_ST.letBool(pdef->id.i, SMTBoolean(false));
            m_ST.letBool(parg->id.i, procInter.ReadBoolean(pdef));
        }

        if (bMoreArgs) {
            pargs = pargs->opr.op[0];
            pdefargs = pdefargs->opr.op[0];
        }
    }
    return true;
}



void SMTInterpreter::RecordProcedure(nodeType *p) {
    URSAprocedures[p->opr.op[0]->id.i] = p;
}

void SMTInterpreter::RecordCommand(nodeType *p) {
    URSAprogram.push_back(p);
}

void SMTInterpreter::ClearCommand(nodeType *p) {
    if (!p) return;
    if (p->type == typeId) free(p->id.i);
    if (p->type == typeOpr)
        for (int i = 0; i < p->opr.nops; i++)
            ClearCommand(p->opr.op[i]);
    free(p);
}

void SMTInterpreter::ClearProgram() {
    for (auto* c : URSAprogram) ClearCommand(c);
    URSAprogram.clear();
}

void SMTInterpreter::ExecuteCommandTree(nodeType *p) {
    if (p->opr.oper == ';') {
        ExecuteCommandTree(p->opr.op[0]);
        ExecuteCommand(p->opr.op[1]);
    } else {
        ExecuteCommand(p);
    }
}

void SMTInterpreter::PrintProcedure() {}
void SMTInterpreter::PrintCommand(nodeType*) {}
