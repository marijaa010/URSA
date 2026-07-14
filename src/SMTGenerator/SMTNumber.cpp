#include "SMTNumber.hpp"
#include "SMTBoolean.hpp"
#include <cstdlib>
#include <iostream>

using namespace std;

// The SMT logic mode is set by the CLI driver in URSA_SATinterpreter.cpp.
// In QF_LIA mode, arithmetic is performed over unbounded integers (no
// modular masking), and bitwise/shift operators are rejected as errors.
typedef enum { eLogicQF_BV, eLogicQF_LIA } eSMTLogic;
extern eSMTLogic bSMTLogic;

static inline bool isLIAMode() { return bSMTLogic == eLogicQF_LIA; }

// Emit a fatal error when a QF_BV-only operator is used in QF_LIA mode.
[[noreturn]] static void liaUnsupported(const char* op) {
    cerr << "ERROR: operator '" << op << "' is not supported in QF_LIA mode."
         << endl
         << "  QF_LIA has no bit-vector or shift operations. Either rewrite"
         << endl
         << "  the program using only linear integer arithmetic, or use the"
         << endl
         << "  default QF_BV mode." << endl;
    exit(1);
}

uint64_t SMTNumber::maskTo(uint64_t v, int width) {
    uint64_t mask = (width >= 64) ? ~0ULL : ((1ULL << width) - 1);
    return v & mask;
}

SMTNumber::SMTNumber()
    : m_expr(nullptr), m_width(0), m_isGround(true), m_groundValue(0) {}

SMTNumber::SMTNumber(int width)
    : m_width(width), m_isGround(true), m_groundValue(0) {
    m_expr = isLIAMode() ? SMTFactory::makeIntConst(0)
                         : SMTFactory::makeBvConst(0, width);
}

SMTNumber::SMTNumber(uint64_t value, int width)
    : m_width(width), m_isGround(true) {
    if (isLIAMode()) {
        // No modular masking in LIA — value is a true integer.
        m_groundValue = value;
        m_expr = SMTFactory::makeIntConst(value);
    } else {
        m_groundValue = maskTo(value, width);
        m_expr = SMTFactory::makeBvConst(value, width);
    }
}

SMTNumber::SMTNumber(const string& varName, int width)
    : m_width(width), m_isGround(false), m_groundValue(0) {
    m_expr = isLIAMode() ? SMTFactory::makeIntVar(varName)
                         : SMTFactory::makeBvVar(varName, width);
}

SMTNumber::SMTNumber(SMTExpr* expr, bool isGround, uint64_t groundVal)
    : m_expr(expr), m_width(expr ? expr->width : 0),
      m_isGround(isGround),
      m_groundValue(!isGround ? 0
                    : (isLIAMode() ? groundVal : maskTo(groundVal, m_width))) {}

// All-ones constant for the current width, e.g. 0xFF for width=8.
static inline uint64_t allOnes(int width) {
    return (width >= 64) ? ~0ULL : ((1ULL << width) - 1);
}

SMTNumber SMTNumber::operator+(const SMTNumber& other) const {
    if (m_isGround && other.m_isGround) {
        uint64_t r = m_groundValue + other.m_groundValue;
        if (!isLIAMode()) r = maskTo(r, m_width);
        return SMTNumber(r, m_width);
    }
    if (m_isGround && m_groundValue == 0) return other;   // 0 + X = X
    if (other.m_isGround && other.m_groundValue == 0) return *this;  // X + 0 = X
    SMTExpr* node = isLIAMode() ? SMTFactory::makeIntAdd(m_expr, other.m_expr)
                                : SMTFactory::makeBvAdd(m_expr, other.m_expr);
    return SMTNumber(node);
}

SMTNumber SMTNumber::operator-(const SMTNumber& other) const {
    if (m_isGround && other.m_isGround) {
        uint64_t r = m_groundValue - other.m_groundValue;
        if (!isLIAMode()) r = maskTo(r, m_width);
        return SMTNumber(r, m_width);
    }
    if (other.m_isGround && other.m_groundValue == 0) return *this;  // X - 0 = X
    if (m_isGround && m_groundValue == 0) return other.negate();      // 0 - X = -X
    SMTExpr* node = isLIAMode() ? SMTFactory::makeIntSub(m_expr, other.m_expr)
                                : SMTFactory::makeBvSub(m_expr, other.m_expr);
    return SMTNumber(node);
}

SMTNumber SMTNumber::operator*(const SMTNumber& other) const {
    if (m_isGround && other.m_isGround) {
        uint64_t r = m_groundValue * other.m_groundValue;
        if (!isLIAMode()) r = maskTo(r, m_width);
        return SMTNumber(r, m_width);
    }
    // 0 * X = 0, X * 0 = 0
    if (m_isGround && m_groundValue == 0) return *this;
    if (other.m_isGround && other.m_groundValue == 0) return other;
    // 1 * X = X, X * 1 = X
    if (m_isGround && m_groundValue == 1) return other;
    if (other.m_isGround && other.m_groundValue == 1) return *this;
    // In LIA, nonlinear multiplication (var * var) is outside the theory
    // fragment. Reject with a clear error.
    if (isLIAMode() && !m_isGround && !other.m_isGround) {
        cerr << "ERROR: nonlinear multiplication (var * var) is not allowed"
             << " in QF_LIA mode." << endl;
        exit(1);
    }
    SMTExpr* node = isLIAMode() ? SMTFactory::makeIntMul(m_expr, other.m_expr)
                                : SMTFactory::makeBvMul(m_expr, other.m_expr);
    return SMTNumber(node);
}

SMTNumber SMTNumber::operator&(const SMTNumber& other) const {
    if (isLIAMode()) liaUnsupported("&");
    if (m_isGround && other.m_isGround) {
        uint64_t r = maskTo(m_groundValue & other.m_groundValue, m_width);
        return SMTNumber(SMTFactory::makeBvConst(r, m_width), true, r);
    }
    // X & 0 = 0
    if (m_isGround && m_groundValue == 0) return *this;
    if (other.m_isGround && other.m_groundValue == 0) return other;
    // X & all_ones = X
    uint64_t ones = allOnes(m_width);
    if (m_isGround && m_groundValue == ones) return other;
    if (other.m_isGround && other.m_groundValue == ones) return *this;
    return SMTNumber(SMTFactory::makeBvAnd(m_expr, other.m_expr));
}

SMTNumber SMTNumber::operator|(const SMTNumber& other) const {
    if (isLIAMode()) liaUnsupported("|");
    if (m_isGround && other.m_isGround) {
        uint64_t r = maskTo(m_groundValue | other.m_groundValue, m_width);
        return SMTNumber(SMTFactory::makeBvConst(r, m_width), true, r);
    }
    // X | 0 = X
    if (m_isGround && m_groundValue == 0) return other;
    if (other.m_isGround && other.m_groundValue == 0) return *this;
    // X | all_ones = all_ones
    uint64_t ones = allOnes(m_width);
    if (m_isGround && m_groundValue == ones) return *this;
    if (other.m_isGround && other.m_groundValue == ones) return other;
    return SMTNumber(SMTFactory::makeBvOr(m_expr, other.m_expr));
}

SMTNumber SMTNumber::operator^(const SMTNumber& other) const {
    if (isLIAMode()) liaUnsupported("^");
    if (m_isGround && other.m_isGround) {
        uint64_t r = maskTo(m_groundValue ^ other.m_groundValue, m_width);
        return SMTNumber(SMTFactory::makeBvConst(r, m_width), true, r);
    }
    // X ^ 0 = X
    if (m_isGround && m_groundValue == 0) return other;
    if (other.m_isGround && other.m_groundValue == 0) return *this;
    return SMTNumber(SMTFactory::makeBvXor(m_expr, other.m_expr));
}

SMTNumber SMTNumber::operator<<(const SMTNumber& other) const {
    if (isLIAMode()) liaUnsupported("<<");
    if (m_isGround && other.m_isGround) {
        uint64_t sh = other.m_groundValue;
        uint64_t r = (sh >= 64) ? 0 : maskTo(m_groundValue << sh, m_width);
        return SMTNumber(SMTFactory::makeBvConst(r, m_width), true, r);
    }
    // X << 0 = X
    if (other.m_isGround && other.m_groundValue == 0) return *this;
    return SMTNumber(SMTFactory::makeBvShl(m_expr, other.m_expr));
}

SMTNumber SMTNumber::operator>>(const SMTNumber& other) const {
    if (isLIAMode()) liaUnsupported(">>");
    if (m_isGround && other.m_isGround) {
        uint64_t sh = other.m_groundValue;
        uint64_t r = (sh >= 64) ? 0 : (m_groundValue >> sh);
        return SMTNumber(SMTFactory::makeBvConst(r, m_width), true, r);
    }
    // X >> 0 = X
    if (other.m_isGround && other.m_groundValue == 0) return *this;
    return SMTNumber(SMTFactory::makeBvLshr(m_expr, other.m_expr));
}

SMTNumber SMTNumber::negate() const {
    if (m_isGround) {
        uint64_t r = m_groundValue;
        if (isLIAMode()) {
            // In LIA, unary minus on ground gives the negation. We store the
            // absolute value and emit an INT_NEG node.
            SMTExpr* zero = SMTFactory::makeIntConst(0);
            SMTExpr* val  = SMTFactory::makeIntConst(r);
            return SMTNumber(SMTFactory::makeIntSub(zero, val));
        }
        r = maskTo(0 - r, m_width);
        return SMTNumber(SMTFactory::makeBvConst(r, m_width), true, r);
    }
    SMTExpr* node = isLIAMode() ? SMTFactory::makeIntNeg(m_expr)
                                : SMTFactory::makeBvNeg(m_expr);
    return SMTNumber(node);
}

SMTNumber SMTNumber::bitnegate() const {
    if (isLIAMode()) liaUnsupported("~");
    if (m_isGround) {
        uint64_t r = maskTo(~m_groundValue, m_width);
        return SMTNumber(SMTFactory::makeBvConst(r, m_width), true, r);
    }
    return SMTNumber(SMTFactory::makeBvNot(m_expr));
}

SMTNumber SMTNumber::sgn() const {
    if (m_isGround) {
        uint64_t r = (m_groundValue != 0) ? 1 : 0;
        return SMTNumber(r, m_width);
    }
    if (isLIAMode()) {
        SMTExpr* zero = SMTFactory::makeIntConst(0);
        SMTExpr* one  = SMTFactory::makeIntConst(1);
        SMTExpr* isZero = SMTFactory::makeIntEq(m_expr, zero);
        return SMTNumber(SMTFactory::makeIte(isZero, zero, one));
    }
    SMTExpr* zero = SMTFactory::makeBvConst(0, m_width);
    SMTExpr* one  = SMTFactory::makeBvConst(1, m_width);
    SMTExpr* isZero = SMTFactory::makeBvEq(m_expr, zero);
    return SMTNumber(SMTFactory::makeIte(isZero, zero, one));
}

#define REL_OP(OP_CPP, BV_MAKE, INT_MAKE) \
    if (m_isGround && other.m_isGround) { \
        bool r = (m_groundValue OP_CPP other.m_groundValue); \
        return SMTBoolean(SMTFactory::makeBoolConst(r), true, r); \
    } \
    if (isLIAMode()) \
        return SMTBoolean(SMTFactory::INT_MAKE(m_expr, other.m_expr)); \
    return SMTBoolean(SMTFactory::BV_MAKE(m_expr, other.m_expr));

SMTBoolean SMTNumber::operator==(const SMTNumber& other) const { REL_OP(==, makeBvEq,  makeIntEq) }
SMTBoolean SMTNumber::operator<(const SMTNumber& other) const  { REL_OP(<,  makeBvUlt, makeIntLt) }
SMTBoolean SMTNumber::operator>(const SMTNumber& other) const  { REL_OP(>,  makeBvUgt, makeIntGt) }
SMTBoolean SMTNumber::operator<=(const SMTNumber& other) const { REL_OP(<=, makeBvUle, makeIntLe) }
SMTBoolean SMTNumber::operator>=(const SMTNumber& other) const { REL_OP(>=, makeBvUge, makeIntGe) }

#undef REL_OP

SMTBoolean SMTNumber::operator!=(const SMTNumber& other) const {
    if (m_isGround && other.m_isGround) {
        bool r = (m_groundValue != other.m_groundValue);
        return SMTBoolean(SMTFactory::makeBoolConst(r), true, r);
    }
    SMTExpr* eq = isLIAMode() ? SMTFactory::makeIntEq(m_expr, other.m_expr)
                              : SMTFactory::makeBvEq(m_expr, other.m_expr);
    return SMTBoolean(SMTFactory::makeBoolNot(eq));
}

SMTNumber SMTNumber::ite(const SMTBoolean& cond, const SMTNumber& other) const {
    if (cond.IsGroundBoolean()) {
        return cond.GetGroundValue() ? *this : other;
    }
    return SMTNumber(SMTFactory::makeIte(cond.getExpr(), m_expr, other.m_expr));
}

SMTBoolean SMTNumber::Bool() const {
    if (m_isGround) {
        bool r = (m_groundValue != 0);
        return SMTBoolean(SMTFactory::makeBoolConst(r), true, r);
    }
    if (isLIAMode()) {
        SMTExpr* zero = SMTFactory::makeIntConst(0);
        SMTExpr* isZero = SMTFactory::makeIntEq(m_expr, zero);
        return SMTBoolean(SMTFactory::makeBoolNot(isZero));
    }
    SMTExpr* zero = SMTFactory::makeBvConst(0, m_width);
    SMTExpr* isZero = SMTFactory::makeBvEq(m_expr, zero);
    return SMTBoolean(SMTFactory::makeBoolNot(isZero));
}

void SMTNumber::print(ostream& out) const {
    if (m_expr) m_expr->print(out);
}
