/**
 * @file SMTSymbolTable.hpp
 * @brief Symbol table tracking numeric, Boolean, and array variables during SMT emission.
 *
 * Parallel to the SAT-side SymbolTable, but stores SMTNumber / SMTBoolean
 * / SMTExpr values instead of Number / Boolean / FormulaVector.
 *
 * Key responsibility: array materialization. URSA programs that only
 * write ground-index cells (e.g., `nA[3] = 5`) are emitted as flat scalar
 * variables `nA_3_`, `nA_4_`, …. As soon as the same array is accessed
 * with a symbolic index anywhere in the program, the table
 * "materializes" it into a real SMT-LIB `(Array …)` value, re-emitting
 * previously-seen ground writes as `(assert (= (select nA k) v))` and
 * routing subsequent reads/writes through select/store expressions
 * (with SSA versioning for stores).
 *
 * That's why HasMaterializedArrays() must be consulted at emit time to
 * choose between QF_BV/QF_LIA and their array-extended counterparts
 * QF_ABV / QF_ALIA.
 */

#ifndef __SMT_SYMBOL_TABLE
#define __SMT_SYMBOL_TABLE

#include <map>
#include <string>
#include <iostream>
#include "SMTNumber.hpp"
#include "SMTBoolean.hpp"

/**
 * @brief Runtime symbol table for the SMT emission path.
 *
 * Holds three kinds of mappings:
 *   - Scalar numeric variables: name → SMTNumber*
 *   - Scalar Boolean variables: name → SMTBoolean*
 *   - Arrays: name → base value (creation-time snapshot) + current
 *     version (after successive stores). Both are SMTExpr* referring
 *     to nodes in SMTFactory's cache.
 *
 * Array cells with ground indices are initially stored as ordinary
 * scalars under synthesized names like `nA_3_`. This flat form is
 * discarded (and replayed as `(assert (= (select nA k) v))`) the first
 * time the array is touched with a symbolic index.
 */
class SMTSymbolTable {

public:
  SMTSymbolTable() {}
  ~SMTSymbolTable() { Clear(); }

  /** @brief Frees all stored SMTNumber / SMTBoolean instances. */
  void Clear();

  /// @name Numeric variables
  ///@{
  /** @brief Whether the given name is bound to a numeric value. */
  bool DefinedIntVar(const std::string& sVarName);

  /** @brief Binds `sVarName` to `nValue` (overwrites if bound). */
  void letInt(const std::string& sVarName, const SMTNumber& nValue);

  /**
   * @brief Writes `nValue` at position `nIndex` of the 1D array `sVarName`.
   *
   * If `nIndex` is ground and the array has not been materialized yet,
   * the write is stored as a scalar `sVarName_<idx>_`. Otherwise the
   * array is materialized (if not already) and a `(store …)` expression
   * becomes the new current version.
   */
  void letIntEl(const std::string& sVarName, const SMTNumber& nIndex, const SMTNumber& nValue);

  /** @brief 2D array write. Ground-index cells go into `sVarName_i__j_` scalars. */
  void letIntEl2(const std::string& sVarName, const SMTNumber& nIndex1,
                 const SMTNumber& nIndex2, const SMTNumber& nValue);

  /** @brief Reads the value bound to `sVarName`, or creates a fresh free variable. */
  SMTNumber getIntValue(const std::string& sVarName);

  /** @brief Reads array cell, and may trigger materialization if the index is symbolic. */
  SMTNumber getIntElValue(const std::string& sVarName, const SMTNumber& nIndex);
  SMTNumber getIntElValue2(const std::string& sVarName,
                           const SMTNumber& nIndex1, const SMTNumber& nIndex2);
  ///@}

  /// @name Boolean variables (same shape as numeric, no array materialization)
  ///@{
  bool DefinedBoolVar(const std::string& sVarName);
  void letBool(const std::string& sVarName, const SMTBoolean& bValue);
  void letBoolEl(const std::string& sVarName, const SMTNumber& nIndex, const SMTBoolean& bValue);
  void letBoolEl2(const std::string& sVarName, const SMTNumber& nIndex1,
                  const SMTNumber& nIndex2, const SMTBoolean& bValue);

  SMTBoolean getBoolValue(const std::string& sVarName);
  SMTBoolean getBoolElValue(const std::string& sVarName, const SMTNumber& nIndex);
  SMTBoolean getBoolElValue2(const std::string& sVarName,
                             const SMTNumber& nIndex1, const SMTNumber& nIndex2);
  ///@}

  /// @name Access-tracking (used by procedure epilogue to detect unused parameters)
  ///@{
  bool SetAccessedIntVar(const std::string& sVarName, bool bA);
  bool GetAccessedIntVar(const std::string& sVarName);
  bool SetAccessedBoolVar(const std::string& sVarName, bool bA);
  bool GetAccessedBoolVar(const std::string& sVarName);
  ///@}

  /// @name Emission helpers used by SMT_Interpreter::SolveConstraint
  ///@{
  /**
   * @brief Emits `(declare-fun name () sort)` for every free variable
   *        (scalar numeric, scalar Boolean, materialized array).
   */
  void collectFreeVarDeclarations(std::ostream& out) const;

  /**
   * @brief Emits `(assert (= (select nA k) v))` for each ground-index
   *        write that occurred before the array was materialized.
   *        Only called when arrays are materialized.
   */
  void collectArrayInitAssertions(std::ostream& out) const;

  /** @brief True if at least one array has been materialized in this session. */
  bool HasMaterializedArrays() const { return !SymArrayBase.empty(); }
  ///@}

private:
  std::map<std::string, SMTNumber*> SymInt;
  std::map<std::string, SMTBoolean*> SymBool;
  std::map<std::string, bool> AccessedInt;
  std::map<std::string, bool> AccessedBool;

  std::map<std::string, SMTExpr*> SymArrayBase;      ///< The declare-fun'd array.
  std::map<std::string, SMTExpr*> SymArrayCurrent;   ///< SSA-current version after stores.

  /**
   * @brief Promotes a 1D array from flat scalar cells to `(Array …)` form.
   *        Emitted ground-index writes become deferred init assertions.
   */
  void materializeArray1D(const std::string& sVarName);

  /** @brief 2D version, using a nested `(Array (_ BitVec ..) (Array (_ BitVec ..) …))`. */
  void materializeArray2D(const std::string& sVarName);
};

#endif
