#include "SMTSymbolTable.hpp"
#include <sstream>
#include <cassert>

extern unsigned int iAbstractNumberLength;

typedef enum { eLogicQF_BV, eLogicQF_LIA } eSMTLogic;
extern eSMTLogic bSMTLogic;

using namespace std;

static bool symbolNeedsQuoting(const string& s) {
    if (s.empty()) return true;
    for (char c : s) {
        if (c >= 'a' && c <= 'z') continue;
        if (c >= 'A' && c <= 'Z') continue;
        if (c >= '0' && c <= '9') continue;
        if (c == '+' || c == '-' || c == '/' || c == '*' || c == '=' ||
            c == '%' || c == '?' || c == '!' || c == '.' || c == '$' ||
            c == '_' || c == '~' || c == '&' || c == '^' || c == '<' ||
            c == '>' || c == '@') continue;
        return true;
    }
    return false;
}

static string quoteSymbol(const string& s) {
    return symbolNeedsQuoting(s) ? ("|" + s + "|") : s;
}

static string itos(uint64_t i) {
    stringstream ss;
    ss << i;
    return ss.str();
}

static string arrayElemName(const string& base, uint64_t idx) {
    return base + "_" + itos(idx) + "_";
}

static string arrayElemName2(const string& base, uint64_t idx1, uint64_t idx2) {
    return base + "_" + itos(idx1) + "__" + itos(idx2) + "_";
}

static SMTExpr* makeIndexConst(uint64_t idx, int width) {
    return (bSMTLogic == eLogicQF_LIA)
        ? SMTFactory::makeIntConst(idx)
        : SMTFactory::makeBvConst(idx, width);
}

void SMTSymbolTable::Clear() {
    for (auto& kv : SymInt) delete kv.second;
    for (auto& kv : SymBool) delete kv.second;
    SymInt.clear();
    SymBool.clear();
    AccessedInt.clear();
    AccessedBool.clear();
    SymArrayBase.clear();
    SymArrayCurrent.clear();
}

void SMTSymbolTable::materializeArray1D(const string& sVarName) {
    if (SymArrayBase.count(sVarName) > 0) return;
    int w = (bSMTLogic == eLogicQF_LIA) ? 0 : (int)iAbstractNumberLength;
    SMTExpr* base = SMTFactory::makeArrayVar(sVarName, w, w);
    SymArrayBase[sVarName] = base;
    SymArrayCurrent[sVarName] = base;
}

void SMTSymbolTable::materializeArray2D(const string& sVarName) {
    if (SymArrayBase.count(sVarName) > 0) return;
    int w = (bSMTLogic == eLogicQF_LIA) ? 0 : (int)iAbstractNumberLength;
    SMTExpr* base = SMTFactory::makeArrayVar2D(sVarName, w, w, w);
    SymArrayBase[sVarName] = base;
    SymArrayCurrent[sVarName] = base;
}

bool SMTSymbolTable::SetAccessedIntVar(const string& s, bool bA) {
    if (SymInt.find(s) == SymInt.end()) return false;
    AccessedInt[s] = bA;
    return true;
}
bool SMTSymbolTable::GetAccessedIntVar(const string& s) {
    auto it = AccessedInt.find(s);
    return it != AccessedInt.end() && it->second;
}
bool SMTSymbolTable::SetAccessedBoolVar(const string& s, bool bA) {
    if (SymBool.find(s) == SymBool.end()) return false;
    AccessedBool[s] = bA;
    return true;
}
bool SMTSymbolTable::GetAccessedBoolVar(const string& s) {
    auto it = AccessedBool.find(s);
    return it != AccessedBool.end() && it->second;
}

bool SMTSymbolTable::DefinedIntVar(const string& s) {
    return SymInt.find(s) != SymInt.end();
}

void SMTSymbolTable::letInt(const string& sVarName, const SMTNumber& nValue) {
    auto it = SymInt.find(sVarName);
    if (it == SymInt.end())
        SymInt[sVarName] = new SMTNumber(nValue);
    else
        *it->second = nValue;
    AccessedInt[sVarName] = true;
}

void SMTSymbolTable::letIntEl(const string& sVarName, const SMTNumber& nIndex,
                              const SMTNumber& nValue) {
    if (SymArrayBase.count(sVarName) > 0) {
        SymArrayCurrent[sVarName] = SMTFactory::makeArrayStore(
            SymArrayCurrent[sVarName], nIndex.getExpr(), nValue.getExpr());
        return;
    }
    if (nIndex.IsGroundNumber()) {
        letInt(arrayElemName(sVarName, nIndex.GetGroundValueUnsigned()), nValue);
        return;
    }
    materializeArray1D(sVarName);
    SymArrayCurrent[sVarName] = SMTFactory::makeArrayStore(
        SymArrayCurrent[sVarName], nIndex.getExpr(), nValue.getExpr());
}

void SMTSymbolTable::letIntEl2(const string& sVarName, const SMTNumber& nIndex1,
                               const SMTNumber& nIndex2, const SMTNumber& nValue) {
    bool alreadyMaterialized = SymArrayBase.count(sVarName) > 0;
    bool anySymbolic = !nIndex1.IsGroundNumber() || !nIndex2.IsGroundNumber();
    if (alreadyMaterialized || anySymbolic) {
        materializeArray2D(sVarName);
        SMTExpr* current = SymArrayCurrent[sVarName];
        SMTExpr* innerSel = SMTFactory::makeArraySelect(current, nIndex1.getExpr());
        SMTExpr* newInner = SMTFactory::makeArrayStore(innerSel, nIndex2.getExpr(), nValue.getExpr());
        SymArrayCurrent[sVarName] = SMTFactory::makeArrayStore(current, nIndex1.getExpr(), newInner);
        return;
    }
    letInt(arrayElemName2(sVarName,
                          nIndex1.GetGroundValueUnsigned(),
                          nIndex2.GetGroundValueUnsigned()),
           nValue);
}

SMTNumber SMTSymbolTable::getIntValue(const string& sVarName) {
    auto it = SymInt.find(sVarName);
    if (it == SymInt.end()) {
        SMTNumber* n = new SMTNumber(sVarName, (int)iAbstractNumberLength);
        SymInt[sVarName] = n;
        AccessedInt[sVarName] = true;
        return *n;
    }
    AccessedInt[sVarName] = true;
    return *it->second;
}

SMTNumber SMTSymbolTable::getIntElValue(const string& sVarName, const SMTNumber& nIndex) {
    if (SymArrayBase.count(sVarName) > 0) {
        SMTExpr* sel = SMTFactory::makeArraySelect(SymArrayCurrent[sVarName], nIndex.getExpr());
        return SMTNumber(sel);
    }
    if (nIndex.IsGroundNumber()) {
        return getIntValue(arrayElemName(sVarName, nIndex.GetGroundValueUnsigned()));
    }
    materializeArray1D(sVarName);
    SMTExpr* sel = SMTFactory::makeArraySelect(SymArrayCurrent[sVarName], nIndex.getExpr());
    return SMTNumber(sel);
}

SMTNumber SMTSymbolTable::getIntElValue2(const string& sVarName,
                                        const SMTNumber& nIndex1, const SMTNumber& nIndex2) {
    bool alreadyMaterialized = SymArrayBase.count(sVarName) > 0;
    bool anySymbolic = !nIndex1.IsGroundNumber() || !nIndex2.IsGroundNumber();
    if (alreadyMaterialized || anySymbolic) {
        materializeArray2D(sVarName);
        SMTExpr* innerSel = SMTFactory::makeArraySelect(SymArrayCurrent[sVarName], nIndex1.getExpr());
        SMTExpr* sel = SMTFactory::makeArraySelect(innerSel, nIndex2.getExpr());
        return SMTNumber(sel);
    }
    return getIntValue(arrayElemName2(sVarName,
                                       nIndex1.GetGroundValueUnsigned(),
                                       nIndex2.GetGroundValueUnsigned()));
}

bool SMTSymbolTable::DefinedBoolVar(const string& s) {
    return SymBool.find(s) != SymBool.end();
}

void SMTSymbolTable::letBool(const string& sVarName, const SMTBoolean& bValue) {
    auto it = SymBool.find(sVarName);
    if (it == SymBool.end())
        SymBool[sVarName] = new SMTBoolean(bValue);
    else
        *it->second = bValue;
    AccessedBool[sVarName] = true;
}

void SMTSymbolTable::letBoolEl(const string& sVarName, const SMTNumber& nIndex,
                               const SMTBoolean& bValue) {
    if (!nIndex.IsGroundNumber()) {
        cerr << "ERROR: symbolic index on Bool array '" << sVarName
             << "' is not supported. Only numeric arrays support symbolic indexing."
             << endl;
        exit(1);
    }
    letBool(arrayElemName(sVarName, nIndex.GetGroundValueUnsigned()), bValue);
}

void SMTSymbolTable::letBoolEl2(const string& sVarName, const SMTNumber& nIndex1,
                                const SMTNumber& nIndex2, const SMTBoolean& bValue) {
    if (!nIndex1.IsGroundNumber() || !nIndex2.IsGroundNumber()) {
        cerr << "ERROR: symbolic index on Bool array '" << sVarName
             << "' is not supported. Only numeric arrays support symbolic indexing."
             << endl;
        exit(1);
    }
    letBool(arrayElemName2(sVarName,
                           nIndex1.GetGroundValueUnsigned(),
                           nIndex2.GetGroundValueUnsigned()),
            bValue);
}

SMTBoolean SMTSymbolTable::getBoolValue(const string& sVarName) {
    auto it = SymBool.find(sVarName);
    if (it == SymBool.end()) {
        SMTBoolean* b = new SMTBoolean(SMTFactory::makeBoolVar(sVarName), false, false);
        SymBool[sVarName] = b;
        AccessedBool[sVarName] = true;
        return *b;
    }
    AccessedBool[sVarName] = true;
    return *it->second;
}

SMTBoolean SMTSymbolTable::getBoolElValue(const string& sVarName, const SMTNumber& nIndex) {
    if (!nIndex.IsGroundNumber()) {
        cerr << "ERROR: symbolic index on Bool array '" << sVarName
             << "' is not supported. Only numeric arrays support symbolic indexing."
             << endl;
        exit(1);
    }
    return getBoolValue(arrayElemName(sVarName, nIndex.GetGroundValueUnsigned()));
}

SMTBoolean SMTSymbolTable::getBoolElValue2(const string& sVarName,
                                          const SMTNumber& nIndex1, const SMTNumber& nIndex2) {
    if (!nIndex1.IsGroundNumber() || !nIndex2.IsGroundNumber()) {
        cerr << "ERROR: symbolic index on Bool array '" << sVarName
             << "' is not supported. Only numeric arrays support symbolic indexing."
             << endl;
        exit(1);
    }
    return getBoolValue(arrayElemName2(sVarName,
                                        nIndex1.GetGroundValueUnsigned(),
                                        nIndex2.GetGroundValueUnsigned()));
}

void SMTSymbolTable::collectFreeVarDeclarations(ostream& out) const {
    for (auto& kv : SymInt) {
        SMTExpr* e = kv.second->getExpr();
        if (!e || e->varName != kv.first) continue;
        bool ownedByArray = false;
        for (auto& akv : SymArrayBase) {
            const std::string& base = akv.first;
            if (kv.first.size() > base.size() + 2 &&
                kv.first.compare(0, base.size(), base) == 0 &&
                kv.first[base.size()] == '_') {
                ownedByArray = true;
                break;
            }
        }
        if (ownedByArray) continue;
        if (e->type == BV_VAR) {
            out << "(declare-fun " << quoteSymbol(kv.first)
                << " () (_ BitVec " << kv.second->getWidth() << "))" << endl;
        } else if (e->type == INT_VAR) {
            out << "(declare-fun " << quoteSymbol(kv.first) << " () Int)" << endl;
        }
    }
    for (auto& kv : SymBool) {
        SMTExpr* e = kv.second->getExpr();
        if (e && e->type == BOOL_VAR && e->varName == kv.first) {
            out << "(declare-fun " << quoteSymbol(kv.first) << " () Bool)" << endl;
        }
    }
    for (auto& kv : SymArrayBase) {
        SMTExpr* arr = kv.second;
        out << "(declare-fun " << quoteSymbol(kv.first) << " () ";
        if (arr->is2D) {
            if (bSMTLogic == eLogicQF_LIA) {
                out << "(Array Int (Array Int Int))";
            } else {
                out << "(Array (_ BitVec " << arr->indexWidth << ") "
                    << "(Array (_ BitVec " << arr->indexWidth2 << ") "
                    << "(_ BitVec " << arr->width << ")))";
            }
        } else {
            if (bSMTLogic == eLogicQF_LIA) {
                out << "(Array Int Int)";
            } else {
                out << "(Array (_ BitVec " << arr->indexWidth << ") "
                    << "(_ BitVec " << arr->width << "))";
            }
        }
        out << ")" << endl;
    }
}

void SMTSymbolTable::collectArrayInitAssertions(ostream& out) const {
    if (SymArrayBase.empty()) return;
    for (auto& akv : SymArrayBase) {
        const std::string& base = akv.first;
        SMTExpr* arr = akv.second;
        for (auto& kv : SymInt) {
            const std::string& name = kv.first;
            if (name.size() <= base.size() + 2) continue;
            if (name.compare(0, base.size(), base) != 0) continue;
            if (name[base.size()] != '_') continue;
            if (name.back() != '_') continue;
            size_t start = base.size() + 1;
            std::string rest = name.substr(start, name.size() - start - 1);
            if (rest.empty()) continue;
            size_t doubleUnderscore = rest.find("__");
            if (arr->is2D) {
                if (doubleUnderscore == std::string::npos) continue;
                std::string idx1Str = rest.substr(0, doubleUnderscore);
                std::string idx2Str = rest.substr(doubleUnderscore + 2);
                if (idx1Str.empty() || idx2Str.empty()) continue;
                bool ok = true;
                for (char c : idx1Str) if (c < '0' || c > '9') { ok = false; break; }
                for (char c : idx2Str) if (c < '0' || c > '9') { ok = false; break; }
                if (!ok) continue;
                uint64_t idx1 = std::stoull(idx1Str);
                uint64_t idx2 = std::stoull(idx2Str);
                SMTExpr* idx1Expr = makeIndexConst(idx1, arr->indexWidth);
                SMTExpr* idx2Expr = makeIndexConst(idx2, arr->indexWidth2);
                SMTExpr* innerSel = SMTFactory::makeArraySelect(arr, idx1Expr);
                SMTExpr* sel = SMTFactory::makeArraySelect(innerSel, idx2Expr);
                SMTExpr* val = kv.second->getExpr();
                out << "(assert (= ";
                sel->print(out);
                out << " ";
                if (val) val->print(out);
                out << "))" << endl;
            } else {
                if (doubleUnderscore != std::string::npos) continue;
                bool ok = true;
                for (char c : rest) if (c < '0' || c > '9') { ok = false; break; }
                if (!ok) continue;
                uint64_t idx = std::stoull(rest);
                SMTExpr* idxExpr = makeIndexConst(idx, arr->indexWidth);
                SMTExpr* sel = SMTFactory::makeArraySelect(arr, idxExpr);
                SMTExpr* val = kv.second->getExpr();
                out << "(assert (= ";
                sel->print(out);
                out << " ";
                if (val) val->print(out);
                out << "))" << endl;
            }
        }
    }
}
