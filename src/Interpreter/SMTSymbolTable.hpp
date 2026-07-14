#ifndef __SMT_SYMBOL_TABLE
#define __SMT_SYMBOL_TABLE

#include <map>
#include <string>
#include <iostream>
#include "SMTNumber.hpp"
#include "SMTBoolean.hpp"

class SMTSymbolTable {

public:
  SMTSymbolTable() {}
  ~SMTSymbolTable() { Clear(); }

  void Clear();

  bool DefinedIntVar(const std::string& sVarName);
  void letInt(const std::string& sVarName, const SMTNumber& nValue);
  void letIntEl(const std::string& sVarName, const SMTNumber& nIndex, const SMTNumber& nValue);
  void letIntEl2(const std::string& sVarName, const SMTNumber& nIndex1,
                 const SMTNumber& nIndex2, const SMTNumber& nValue);

  SMTNumber getIntValue(const std::string& sVarName);
  SMTNumber getIntElValue(const std::string& sVarName, const SMTNumber& nIndex);
  SMTNumber getIntElValue2(const std::string& sVarName,
                           const SMTNumber& nIndex1, const SMTNumber& nIndex2);

  bool DefinedBoolVar(const std::string& sVarName);
  void letBool(const std::string& sVarName, const SMTBoolean& bValue);
  void letBoolEl(const std::string& sVarName, const SMTNumber& nIndex, const SMTBoolean& bValue);
  void letBoolEl2(const std::string& sVarName, const SMTNumber& nIndex1,
                  const SMTNumber& nIndex2, const SMTBoolean& bValue);

  SMTBoolean getBoolValue(const std::string& sVarName);
  SMTBoolean getBoolElValue(const std::string& sVarName, const SMTNumber& nIndex);
  SMTBoolean getBoolElValue2(const std::string& sVarName,
                             const SMTNumber& nIndex1, const SMTNumber& nIndex2);

  bool SetAccessedIntVar(const std::string& sVarName, bool bA);
  bool GetAccessedIntVar(const std::string& sVarName);
  bool SetAccessedBoolVar(const std::string& sVarName, bool bA);
  bool GetAccessedBoolVar(const std::string& sVarName);

  void collectFreeVarDeclarations(std::ostream& out) const;
  void printIndependentNames(std::ostream& out) const;

private:
  std::map<std::string, SMTNumber*> SymInt;
  std::map<std::string, SMTBoolean*> SymBool;
  std::map<std::string, bool> AccessedInt;
  std::map<std::string, bool> AccessedBool;
};

#endif
