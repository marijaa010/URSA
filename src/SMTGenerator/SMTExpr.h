/**
 * @file SMTExpr.h
 * @brief SMT-LIB expression DAG with hash-consing.
 *
 * This module defines the internal representation used by URSA's SMT
 * emission path. Every SMT-LIB expression that URSA builds during
 * program execution (a bit-vector operation, a Boolean, a linear-integer
 * relation, an array read/write) is a node in a DAG whose nodes are
 * instances of SMTExpr. Nodes are never allocated directly by client
 * code, and are always obtained through SMTFactory, which performs
 * hash-consing so structurally equal subexpressions share a single node.
 *
 * The three supported node families are:
 *   - BV_* : fixed-width bit-vector operations (QF_BV logic)
 *   - INT_*: unbounded linear integer operations (QF_LIA logic)
 *   - BOOL_*, ARRAY_*, SMT_ITE: theory-independent building blocks
 *
 * The choice between BV_* and INT_* is driven by the global bSMTLogic
 * at construction time via helpers in SMTNumber / SMTBoolean.
 */

#ifndef __SMT_EXPR_H
#define __SMT_EXPR_H

#include <string>
#include <vector>
#include <iostream>
#include <cstdint>

/**
 * @brief Discriminator for the kind of node an SMTExpr represents.
 *
 * Nodes are grouped by theory. BV_* map to SMT-LIB QF_BV operations,
 * INT_* map to QF_LIA, BOOL_* / ARRAY_* are theory-independent.
 * Adding a new theory means: add enum values here, extend the printer
 * in SMTExpr::print, add factory methods in SMTFactory.
 */
enum SMTNodeType {
    BV_CONST,
    BV_VAR,

    BV_ADD,
    BV_SUB,
    BV_MUL,
    BV_NEG,

    BV_AND,
    BV_OR,
    BV_XOR,
    BV_NOT,

    BV_SHL,
    BV_LSHR,

    BV_UDIV,
    BV_UREM,

    BV_EQ,
    BV_ULT,
    BV_ULE,
    BV_UGT,
    BV_UGE,

    BOOL_CONST,
    BOOL_VAR,
    BOOL_AND,
    BOOL_OR,
    BOOL_XOR,
    BOOL_NOT,
    BOOL_EQ,

    INT_CONST,
    INT_VAR,
    INT_ADD,
    INT_SUB,
    INT_MUL,
    INT_NEG,
    INT_DIV,
    INT_MOD,
    INT_LT,
    INT_LE,
    INT_GT,
    INT_GE,
    INT_EQ,

    ARRAY_VAR,
    ARRAY_SELECT,
    ARRAY_STORE,

    SMT_ITE
};

/**
 * @brief One node in the SMT expression DAG.
 *
 * Nodes are POD-like: type discriminator plus a small handful of
 * payload fields interpreted according to type. The `children` vector
 * points to operand nodes (also hash-consed).
 *
 * Ownership: nodes are owned by SMTFactory's internal cache, not by
 * the pointers stored in `children`. Do NOT delete SMTExpr* pointers
 * manually. Call SMTFactory::clear() at end of session.
 */
class SMTExpr {
public:
    SMTNodeType type;                  ///< Node kind (see SMTNodeType).
    int width;                         ///< Bit-width for BV_*/BOOL, ignored for INT_*.
    std::vector<SMTExpr*> children;    ///< Operand nodes (hash-consed).

    std::string varName;               ///< Set only for *_VAR and *_CONST-with-name nodes.
    uint64_t constValue;               ///< Set only for BV_CONST / INT_CONST.
    bool boolValue;                    ///< Set only for BOOL_CONST.

    int indexWidth;                    ///< For ARRAY_VAR: bit-width of the index sort.
    int indexWidth2;                   ///< For 2D ARRAY_VAR: bit-width of the second index.
    bool is2D;                         ///< True for nested-array (2D) form.

    /**
     * @brief Constructs a bare node. Child pointers and payload are set
     *        by the factory afterwards.
     * @param t Node type.
     * @param w Bit-width (BV) or ignored (INT/BOOL).
     */
    SMTExpr(SMTNodeType t, int w)
        : type(t), width(w), constValue(0), boolValue(false),
          indexWidth(0), indexWidth2(0), is2D(false) {}

    /**
     * @brief Emits SMT-LIB s-expression form, no line breaks, no let-binding.
     * @param out Target stream.
     */
    void print(std::ostream& out) const;

    /**
     * @brief Emits multi-line indented SMT-LIB form for readability.
     *        Used for assert bodies that would otherwise be very long.
     * @param out    Target stream.
     * @param indent Leading-space indent for the outermost open paren.
     */
    void printPretty(std::ostream& out, int indent = 0) const;

    /**
     * @brief Emits SMT-LIB form with `(let ...)` binding of shared subexpressions.
     *
     * Traverses the DAG counting node uses. Any subexpression referenced
     * more than once is bound to a fresh let-variable so it appears once
     * in the output. Significantly reduces size of assertions with
     * repeated common subexpressions (e.g., unrolled loops).
     *
     * @param out    Target stream.
     * @param indent Leading-space indent.
     */
    void printWithLet(std::ostream& out, int indent = 0) const;

    /**
     * @brief Total node count in the DAG rooted at this node (counting
     *        shared children once per parent, i.e. the *tree* expansion,
     *        not the DAG size).
     *
     * Used only for reporting the hash-consing sharing factor.
     */
    size_t treeSize() const;

    /**
     * @brief Formats a bit-vector literal in `#xNN` or `#bNNN` form
     *        (whichever is shorter for the given width).
     * @param value Numeric value, masked to `width` bits.
     * @param width Bit-width of the literal.
     */
    static std::string formatBvConst(uint64_t value, int width);
};

/**
 * @brief Factory for SMTExpr nodes with hash-consing.
 *
 * All SMTExpr nodes must be created through this class. Never `new`
 * an SMTExpr directly, or hash-consing invariants break. Structurally
 * equal subexpressions (same type, same operands, same payload) share
 * a single node.
 *
 * The internal cache is process-lifetime. Call clear() before program
 * exit for a clean shutdown.
 */
class SMTFactory {
public:
    /// @name Bit-vector constructors (QF_BV theory)
    ///@{
    static SMTExpr* makeBvConst(uint64_t value, int width);
    static SMTExpr* makeBvVar(const std::string& name, int width);

    static SMTExpr* makeBvAdd(SMTExpr* a, SMTExpr* b);
    static SMTExpr* makeBvSub(SMTExpr* a, SMTExpr* b);
    static SMTExpr* makeBvMul(SMTExpr* a, SMTExpr* b);
    static SMTExpr* makeBvNeg(SMTExpr* a);

    static SMTExpr* makeBvAnd(SMTExpr* a, SMTExpr* b);
    static SMTExpr* makeBvOr(SMTExpr* a, SMTExpr* b);
    static SMTExpr* makeBvXor(SMTExpr* a, SMTExpr* b);
    static SMTExpr* makeBvNot(SMTExpr* a);

    static SMTExpr* makeBvShl(SMTExpr* a, SMTExpr* b);
    static SMTExpr* makeBvLshr(SMTExpr* a, SMTExpr* b);
    static SMTExpr* makeBvUdiv(SMTExpr* a, SMTExpr* b);
    static SMTExpr* makeBvUrem(SMTExpr* a, SMTExpr* b);

    static SMTExpr* makeBvEq(SMTExpr* a, SMTExpr* b);
    static SMTExpr* makeBvUlt(SMTExpr* a, SMTExpr* b);
    static SMTExpr* makeBvUle(SMTExpr* a, SMTExpr* b);
    static SMTExpr* makeBvUgt(SMTExpr* a, SMTExpr* b);
    static SMTExpr* makeBvUge(SMTExpr* a, SMTExpr* b);
    ///@}

    /// @name Boolean constructors
    ///@{
    static SMTExpr* makeBoolConst(bool value);
    static SMTExpr* makeBoolVar(const std::string& name);
    static SMTExpr* makeBoolAnd(SMTExpr* a, SMTExpr* b);
    static SMTExpr* makeBoolOr(SMTExpr* a, SMTExpr* b);
    static SMTExpr* makeBoolXor(SMTExpr* a, SMTExpr* b);
    static SMTExpr* makeBoolNot(SMTExpr* a);
    static SMTExpr* makeBoolEq(SMTExpr* a, SMTExpr* b);
    ///@}

    /**
     * @brief If-then-else, polymorphic over BV/INT/BOOL branch types.
     */
    static SMTExpr* makeIte(SMTExpr* cond, SMTExpr* thenE, SMTExpr* elseE);

    /// @name Linear integer constructors (QF_LIA theory)
    ///@{
    static SMTExpr* makeIntConst(uint64_t value);
    static SMTExpr* makeIntVar(const std::string& name);
    static SMTExpr* makeIntAdd(SMTExpr* a, SMTExpr* b);
    static SMTExpr* makeIntSub(SMTExpr* a, SMTExpr* b);
    static SMTExpr* makeIntMul(SMTExpr* a, SMTExpr* b);
    static SMTExpr* makeIntNeg(SMTExpr* a);
    static SMTExpr* makeIntDiv(SMTExpr* a, SMTExpr* b);
    static SMTExpr* makeIntMod(SMTExpr* a, SMTExpr* b);
    static SMTExpr* makeIntLt(SMTExpr* a, SMTExpr* b);
    static SMTExpr* makeIntLe(SMTExpr* a, SMTExpr* b);
    static SMTExpr* makeIntGt(SMTExpr* a, SMTExpr* b);
    static SMTExpr* makeIntGe(SMTExpr* a, SMTExpr* b);
    static SMTExpr* makeIntEq(SMTExpr* a, SMTExpr* b);
    ///@}

    /// @name Array constructors (extends the base logic to QF_ABV / QF_ALIA)
    ///@{
    /**
     * @brief 1D array variable: sort `(Array (_ BitVec indexWidth) (_ BitVec elementWidth))`.
     */
    static SMTExpr* makeArrayVar(const std::string& name,
                                 int indexWidth, int elementWidth);
    /**
     * @brief 2D array variable: nested array,
     *        `(Array (_ BitVec indexWidth1) (Array (_ BitVec indexWidth2) ...))`.
     */
    static SMTExpr* makeArrayVar2D(const std::string& name,
                                   int indexWidth1, int indexWidth2,
                                   int elementWidth);
    static SMTExpr* makeArraySelect(SMTExpr* array, SMTExpr* index);
    static SMTExpr* makeArrayStore(SMTExpr* array, SMTExpr* index, SMTExpr* value);
    ///@}

    /**
     * @brief Clears the hash-cons cache. Called once at program shutdown.
     */
    static void clear();

    /**
     * @brief Number of unique nodes currently held in the cache.
     *        Used for reporting the sharing factor of the emitted formula.
     */
    static size_t cacheSize();
};

#endif
