#include "SMTSymbolTable.hpp"
#include <sstream>
#include <cassert>

extern unsigned int iAbstractNumberLength;

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

// Build array element name using only characters allowed in SMT-LIB simple
// symbols (no `[` or `]`). Convention: `a[3]` becomes `a_3_`, `a[3][5]`
// becomes `a_3__5_`. Chosen after prof. Janičić suggested avoiding quoted
// identifiers where a plain rewrite is enough.
static string arrayElemName(const string& base, uint64_t idx) {
    return base + "_" + itos(idx) + "_";
}

static string arrayElemName2(const string& base, uint64_t idx1, uint64_t idx2) {
    return base + "_" + itos(idx1) + "__" + itos(idx2) + "_";
}

void SMTSymbolTable::Clear() {
    for (auto& kv : SymInt) delete kv.second;
    for (auto& kv : SymBool) delete kv.second;
    SymInt.clear();
    SymBool.clear();
    AccessedInt.clear();
    AccessedBool.clear();
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

// ----------------------------------------------------------------------------

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
    assert(nIndex.IsGroundNumber());
    letInt(arrayElemName(sVarName, nIndex.GetGroundValueUnsigned()), nValue);
}

void SMTSymbolTable::letIntEl2(const string& sVarName, const SMTNumber& nIndex1,
                               const SMTNumber& nIndex2, const SMTNumber& nValue) {
    assert(nIndex1.IsGroundNumber() && nIndex2.IsGroundNumber());
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
    assert(nIndex.IsGroundNumber());
    return getIntValue(arrayElemName(sVarName, nIndex.GetGroundValueUnsigned()));
}

SMTNumber SMTSymbolTable::getIntElValue2(const string& sVarName,
                                        const SMTNumber& nIndex1, const SMTNumber& nIndex2) {
    assert(nIndex1.IsGroundNumber() && nIndex2.IsGroundNumber());
    return getIntValue(arrayElemName2(sVarName,
                                       nIndex1.GetGroundValueUnsigned(),
                                       nIndex2.GetGroundValueUnsigned()));
}

// ----------------------------------------------------------------------------

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
    assert(nIndex.IsGroundNumber());
    letBool(arrayElemName(sVarName, nIndex.GetGroundValueUnsigned()), bValue);
}

void SMTSymbolTable::letBoolEl2(const string& sVarName, const SMTNumber& nIndex1,
                                const SMTNumber& nIndex2, const SMTBoolean& bValue) {
    assert(nIndex1.IsGroundNumber() && nIndex2.IsGroundNumber());
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
    assert(nIndex.IsGroundNumber());
    return getBoolValue(arrayElemName(sVarName, nIndex.GetGroundValueUnsigned()));
}

SMTBoolean SMTSymbolTable::getBoolElValue2(const string& sVarName,
                                          const SMTNumber& nIndex1, const SMTNumber& nIndex2) {
    assert(nIndex1.IsGroundNumber() && nIndex2.IsGroundNumber());
    return getBoolValue(arrayElemName2(sVarName,
                                        nIndex1.GetGroundValueUnsigned(),
                                        nIndex2.GetGroundValueUnsigned()));
}

// ----------------------------------------------------------------------------

void SMTSymbolTable::collectFreeVarDeclarations(ostream& out) const {
    for (auto& kv : SymInt) {
        SMTExpr* e = kv.second->getExpr();
        if (e && e->type == BV_VAR && e->varName == kv.first) {
            out << "(declare-fun " << quoteSymbol(kv.first)
                << " () (_ BitVec " << kv.second->getWidth() << "))" << endl;
        }
    }
    for (auto& kv : SymBool) {
        SMTExpr* e = kv.second->getExpr();
        if (e && e->type == BOOL_VAR && e->varName == kv.first) {
            out << "(declare-fun " << quoteSymbol(kv.first) << " () Bool)" << endl;
        }
    }
}

void SMTSymbolTable::printIndependentNames(ostream& out) const {
    for (auto& kv : SymInt) {
        SMTExpr* e = kv.second->getExpr();
        if (e && e->type == BV_VAR && e->varName == kv.first)
            out << kv.first << " ";
    }
    for (auto& kv : SymBool) {
        SMTExpr* e = kv.second->getExpr();
        if (e && e->type == BOOL_VAR && e->varName == kv.first)
            out << kv.first << " ";
    }
}
