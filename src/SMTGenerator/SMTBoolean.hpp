/**
 * @file SMTBoolean.hpp
 * @brief Typed wrapper around SMTExpr for Boolean values.
 *
 * SMT-side counterpart to the SAT-side Boolean class, offering the same
 * public API shape so that Interpreter and SMTInterpreter can share the
 * structure of their ExecuteCommand implementations.
 *
 * Ground/symbolic dichotomy mirrors SMTNumber. Ground booleans are folded
 * eagerly, and symbolic booleans are represented as SMTExpr nodes from
 * the BOOL_* family.
 */

#ifndef __SMT_BOOLEAN_H
#define __SMT_BOOLEAN_H

#include "SMTExpr.h"
#include <iostream>

class SMTNumber;

/**
 * @brief A Boolean value, ground or symbolic, backed by SMTExpr.
 *
 * Semantics of `ground` are logic-independent. A ground SMTBoolean is
 * always emitted as `true` or `false` in SMT-LIB. Booleans have no
 * width and no theory selection, so they are the same shape in QF_BV
 * and QF_LIA.
 */
class SMTBoolean {
public:
    /** @brief Default constructor, produces a ground `true`. */
    SMTBoolean();

    /** @brief Ground literal. */
    SMTBoolean(bool value);

    /**
     * @brief Wraps an existing SMTExpr*, used by SMTNumber comparisons
     *        and by SMTSymbolTable when materializing Bool array reads.
     * @param expr      DAG node.
     * @param isGround  Set only if expr represents a known constant.
     * @param groundVal Corresponding value if isGround.
     */
    SMTBoolean(SMTExpr* expr, bool isGround = false, bool groundVal = false);

    SMTExpr* getExpr() const { return m_expr; }

    bool IsGroundBoolean() const { return m_isGround; }

    /**
     * @brief Extracts the ground value.
     * Calling this on a symbolic value is a programming error, so guard
     * with IsGroundBoolean() first.
     */
    bool GetGroundValue() const;

    /// @name Logical operators, eagerly folded when both operands are ground
    /// @{
    SMTBoolean operator&(const SMTBoolean& other) const;
    SMTBoolean operator|(const SMTBoolean& other) const;
    SMTBoolean operator^(const SMTBoolean& other) const;

    /** @brief Logical negation (`not`). */
    SMTBoolean negate() const;
    /// @}

    /** @brief `cond ? *this : elseB` on the Boolean domain. */
    SMTBoolean ite(const SMTBoolean& thenB, const SMTBoolean& elseB) const;

    /** @brief Coerces to an SMTNumber: 1 if true, 0 if false. */
    SMTNumber Int() const;

    /** @brief Debug print in SMT-LIB s-expression form. */
    void print(std::ostream& out) const;

private:
    SMTExpr* m_expr;
    bool m_isGround;
    bool m_groundValue;
};

#endif
