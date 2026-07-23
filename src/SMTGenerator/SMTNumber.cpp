#include "SMTNumber.hpp"
#include "SMTBoolean.hpp"
#include <cstdlib>
#include <iostream>

using namespace std;

typedef enum { eLogicQF_BV, eLogicQF_LIA } eSMTLogic;
extern eSMTLogic bSMTLogic;

static inline bool isLIAMode() { return bSMTLogic == eLogicQF_LIA; }

[[noreturn]] static void liaUnsupported(const char* op) {
    cerr << "ERROR: operator '" << op << "' is not supported in QF_LIA mode."
         << endl;
    exit(1);
}

uint64_t SMTNumber::maskTo(uint64_t v, int width) {
    uint64_t mask = (width >= 64) ? ~0ULL : ((1ULL << width) - 1);
    return v & mask;
}

uint64_t SMTNumber::GetGroundValueUnsigned() const {
    if (!m_isGround) {
        cerr << "ERROR: attempted to read a ground value from a symbolic numeric expression."
             << endl;
        exit(1);
    }
    return m_groundValue;
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

static inline uint64_t allOnes(int width) {
    return (width >= 64) ? ~0ULL : ((1ULL << width) - 1);
}

SMTNumber SMTNumber::operator+(const SMTNumber& other) const {
    if (m_isGround && other.m_isGround) {
        uint64_t r = m_groundValue + other.m_groundValue;
        if (!isLIAMode()) r = maskTo(r, m_width);
        return SMTNumber(r, m_width);
    }
    if (m_isGround && m_groundValue == 0) return other;
    if (other.m_isGround && other.m_groundValue == 0) return *this;
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
    if (other.m_isGround && other.m_groundValue == 0) return *this;
    if (m_isGround && m_groundValue == 0) return other.negate();
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
    if (m_isGround && m_groundValue == 0) return *this;
    if (other.m_isGround && other.m_groundValue == 0) return other;
    if (m_isGround && m_groundValue == 1) return other;
    if (other.m_isGround && other.m_groundValue == 1) return *this;
    if (isLIAMode() && !m_isGround && !other.m_isGround) {
        cerr << "ERROR: nonlinear multiplication (var * var) is not allowed"
             << " in QF_LIA mode." << endl;
        exit(1);
    }
    SMTExpr* node = isLIAMode() ? SMTFactory::makeIntMul(m_expr, other.m_expr)
                                : SMTFactory::makeBvMul(m_expr, other.m_expr);
    return SMTNumber(node);
}

SMTNumber SMTNumber::operator/(const SMTNumber& other) const {
    if (m_isGround && other.m_isGround) {
        if (other.m_groundValue == 0) {
            cerr << "ERROR: division by zero in a ground expression." << endl;
            exit(1);
        }
        uint64_t r = m_groundValue / other.m_groundValue;
        if (!isLIAMode()) r = maskTo(r, m_width);
        return SMTNumber(r, m_width);
    }
    if (other.m_isGround && other.m_groundValue == 1) return *this;
    if (m_isGround && m_groundValue == 0) return *this;
    if (isLIAMode() && !other.m_isGround) {
        cerr << "ERROR: division by a symbolic value is nonlinear and not allowed"
             << " in QF_LIA mode. The divisor must be a ground constant." << endl;
        exit(1);
    }
    SMTExpr* node = isLIAMode() ? SMTFactory::makeIntDiv(m_expr, other.m_expr)
                                : SMTFactory::makeBvUdiv(m_expr, other.m_expr);
    return SMTNumber(node);
}

SMTNumber SMTNumber::operator%(const SMTNumber& other) const {
    if (m_isGround && other.m_isGround) {
        if (other.m_groundValue == 0) {
            cerr << "ERROR: modulo by zero in a ground expression." << endl;
            exit(1);
        }
        uint64_t r = m_groundValue % other.m_groundValue;
        if (!isLIAMode()) r = maskTo(r, m_width);
        return SMTNumber(r, m_width);
    }
    if (other.m_isGround && other.m_groundValue == 1) return SMTNumber((uint64_t)0, m_width);
    if (m_isGround && m_groundValue == 0) return *this;
    if (isLIAMode() && !other.m_isGround) {
        cerr << "ERROR: modulo by a symbolic value is nonlinear and not allowed"
             << " in QF_LIA mode. The divisor must be a ground constant." << endl;
        exit(1);
    }
    SMTExpr* node = isLIAMode() ? SMTFactory::makeIntMod(m_expr, other.m_expr)
                                : SMTFactory::makeBvUrem(m_expr, other.m_expr);
    return SMTNumber(node);
}

SMTNumber SMTNumber::operator&(const SMTNumber& other) const {
    if (isLIAMode()) liaUnsupported("&");
    if (m_isGround && other.m_isGround) {
        uint64_t r = maskTo(m_groundValue & other.m_groundValue, m_width);
        return SMTNumber(SMTFactory::makeBvConst(r, m_width), true, r);
    }
    if (m_isGround && m_groundValue == 0) return *this;
    if (other.m_isGround && other.m_groundValue == 0) return other;
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
    if (m_isGround && m_groundValue == 0) return other;
    if (other.m_isGround && other.m_groundValue == 0) return *this;
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
    if (other.m_isGround && other.m_groundValue == 0) return *this;
    return SMTNumber(SMTFactory::makeBvLshr(m_expr, other.m_expr));
}

SMTNumber SMTNumber::negate() const {
    if (m_isGround) {
        uint64_t r = m_groundValue;
        if (isLIAMode()) {
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
