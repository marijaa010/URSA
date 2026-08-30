/**
 * @file SMTNumber.hpp
 * @brief Typed wrapper around SMTExpr for numeric values.
 *
 * SMTNumber is the SMT-side counterpart to the SAT-side Number class.
 * Both offer the same public arithmetic API (operator+, operator*, etc.)
 * so that Interpreter and SMTInterpreter can share the shape of their
 * ExecuteCommand logic, even though internal representations differ.
 *
 * Two representations coexist behind the same interface:
 *   - Ground: value known at emission time, stored in m_groundValue.
 *   - Symbolic: value is an SMTExpr* referencing the DAG.
 *
 * When both operands of an operator are ground, the result is computed
 * eagerly (constant folding). If any operand is symbolic, the
 * result is an SMTExpr node emitted through SMTFactory.
 *
 * The bSMTLogic global (QF_BV vs QF_LIA) determines which SMTNode
 * family is used for construction (BV_* or INT_*). Nonlinearity and
 * bitwise operations are rejected with a runtime error in QF_LIA mode.
 */

#ifndef __SMT_NUMBER_H
#define __SMT_NUMBER_H

#include "SMTExpr.h"
#include <string>
#include <cstdint>

/**
 * @brief Ground integer type used for QF_LIA constant folding.
 *
 * With GMP (the default) it is arbitrary-precision (mpz_class), matching the
 * unbounded integers of QF_LIA. Without GMP it is a 64-bit signed integer and
 * QF_LIA is refused at startup, so this narrower type is used only to compile.
 * QF_BV keeps its fixed-width uint64 ground values and does not use this type.
 */
#ifdef GMP_SUPPORT
#include <gmpxx.h>
typedef mpz_class LiaGround;
#else
typedef int64_t LiaGround;
#endif

class SMTBoolean;

/**
 * @brief A width-typed numeric value, ground or symbolic, backed by SMTExpr.
 *
 * The `m_width` field is meaningful only in QF_BV mode. In QF_LIA it is
 * carried for compatibility but does not restrict the value (Int is
 * unbounded).
 */
class SMTNumber {
public:
    /**
     * @brief Default constructor, produces a fresh anonymous variable
     *        of `iAbstractNumberLength` bits. Used when SymbolTable needs
     *        to reserve an initial slot before assignment.
     */
    SMTNumber();

    /**
     * @brief Fresh anonymous variable of the given width.
     * @param width Bit-width. Ignored in QF_LIA mode.
     */
    SMTNumber(int width);

    /**
     * @brief Ground literal.
     * @param value Numeric value, masked to `width` bits in QF_BV.
     * @param width Bit-width.
     */
    SMTNumber(uint64_t value, int width);

    /**
     * @brief Named free variable, emitted as `(declare-fun name () sort)`
     *        in the resulting SMT-LIB.
     * @param varName Identifier as it appears in the URSA program.
     * @param width   Bit-width. Ignored in QF_LIA (sort is Int).
     */
    SMTNumber(const std::string& varName, int width);

    /**
     * @brief Wraps an existing SMTExpr*, used internally by operators
     *        and by SMTSymbolTable when materializing array reads.
     * @param expr      DAG node this SMTNumber refers to.
     * @param isGround  Set only if `expr` is a constant node AND caller
     *                 knows the numeric value.
     * @param groundVal The corresponding ground value if `isGround`.
     */
    SMTNumber(SMTExpr* expr, bool isGround = false, uint64_t groundVal = 0);

    /**
     * @brief Builds a ground literal from its source text, preserving the
     *        exact value. In QF_LIA the literal is parsed at arbitrary
     *        precision so values beyond 64 bits are not truncated; in
     *        QF_BV it is parsed and masked to `width` bits.
     * @param literal Numeric literal as written in the program
     *                (decimal, `0x` hex, or `0b` binary).
     * @param width   Bit-width, used only in QF_BV.
     */
    static SMTNumber fromIntLiteral(const char* literal, int width);

    SMTExpr* getExpr() const { return m_expr; }
    int getWidth() const { return m_width; }

    bool IsGroundNumber() const { return m_isGround; }

    /**
     * @brief Extracts the ground value of a ground number.
     * Calling this on a symbolic value is a programming error, so guard
     * with IsGroundNumber() first.
     */
    uint64_t GetGroundValueUnsigned() const;

    /// @name Arithmetic operators
    /// @{
    /// Ground+ground is folded at emission time. Otherwise the operator
    /// constructs an SMTExpr node of the appropriate theory (BV_* in QF_BV,
    /// INT_* in QF_LIA). Division or modulo by ground zero throws a
    /// runtime error. In QF_LIA, multiplication of two non-ground operands,
    /// division or modulo by a non-ground operand, and any bitwise or
    /// shift operation throw a runtime error (nonlinear or non-representable).
    SMTNumber operator+(const SMTNumber& other) const;
    SMTNumber operator-(const SMTNumber& other) const;
    SMTNumber operator*(const SMTNumber& other) const;
    SMTNumber operator/(const SMTNumber& other) const;
    SMTNumber operator%(const SMTNumber& other) const;
    SMTNumber operator&(const SMTNumber& other) const;
    SMTNumber operator|(const SMTNumber& other) const;
    SMTNumber operator^(const SMTNumber& other) const;
    SMTNumber operator<<(const SMTNumber& other) const;
    SMTNumber operator>>(const SMTNumber& other) const;
    /// @}

    /** @brief Two's-complement negation (bvneg or `-` in Int). */
    SMTNumber negate() const;

    /** @brief Bitwise complement (bvnot). Rejected in QF_LIA. */
    SMTNumber bitnegate() const;

    /**
     * @brief Sign of the value: 0 if zero, 1 if positive, or `-1`
     *        (mod 2^width in QF_BV) if negative.
     */
    SMTNumber sgn() const;

    /// @name Comparison operators, returning an SMTBoolean
    /// @{
    /// Unsigned in QF_BV (bvult / bvule / …), signed-integer in QF_LIA.
    SMTBoolean operator==(const SMTNumber& other) const;
    SMTBoolean operator!=(const SMTNumber& other) const;
    SMTBoolean operator<(const SMTNumber& other) const;
    SMTBoolean operator>(const SMTNumber& other) const;
    SMTBoolean operator<=(const SMTNumber& other) const;
    SMTBoolean operator>=(const SMTNumber& other) const;
    /// @}

    /**
     * @brief If-then-else: `cond ? *this : other`.
     */
    SMTNumber ite(const SMTBoolean& cond, const SMTNumber& other) const;

    /** @brief Coerces to an SMTBoolean, true iff value is non-zero. */
    SMTBoolean Bool() const;

    /** @brief Debug print in SMT-LIB s-expression form. */
    void print(std::ostream& out) const;

private:
    SMTExpr* m_expr;
    int m_width;
    bool m_isGround;
    uint64_t m_groundValue;   ///< Authoritative ground value in QF_BV.
    LiaGround m_groundBig;     ///< Authoritative ground value in QF_LIA.

    /// Builds a QF_LIA ground SMTNumber from an arbitrary-precision value.
    static SMTNumber liaGround(const LiaGround& v);

    static uint64_t maskTo(uint64_t v, int width);
};

#endif
