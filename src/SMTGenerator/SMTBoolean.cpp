#include "SMTBoolean.hpp"
#include "SMTNumber.hpp"

extern unsigned int iAbstractNumberLength;

// Mirror of the enum in URSA_SATinterpreter.cpp (see SMTNumber.cpp for
// rationale). Used to pick between BV and LIA emit paths for Bool→Number.
typedef enum { eLogicQF_BV, eLogicQF_LIA } eSMTLogic;
extern eSMTLogic bSMTLogic;

using namespace std;

SMTBoolean::SMTBoolean()
    : m_expr(SMTFactory::makeBoolConst(false)),
      m_isGround(true), m_groundValue(false) {}

SMTBoolean::SMTBoolean(bool value)
    : m_expr(SMTFactory::makeBoolConst(value)),
      m_isGround(true), m_groundValue(value) {}

SMTBoolean::SMTBoolean(SMTExpr* expr, bool isGround, bool groundVal)
    : m_expr(expr), m_isGround(isGround),
      m_groundValue(isGround ? groundVal : false) {}

SMTBoolean SMTBoolean::operator&(const SMTBoolean& other) const {
    if (m_isGround && other.m_isGround) {
        bool r = m_groundValue && other.m_groundValue;
        return SMTBoolean(SMTFactory::makeBoolConst(r), true, r);
    }
    // Identity / dominator simplifications:
    //   true  & X  =>  X
    //   false & X  =>  false
    //   X & true   =>  X
    //   X & false  =>  false
    if (m_isGround) return m_groundValue ? other : *this;
    if (other.m_isGround) return other.m_groundValue ? *this : other;
    return SMTBoolean(SMTFactory::makeBoolAnd(m_expr, other.m_expr));
}

SMTBoolean SMTBoolean::operator|(const SMTBoolean& other) const {
    if (m_isGround && other.m_isGround) {
        bool r = m_groundValue || other.m_groundValue;
        return SMTBoolean(SMTFactory::makeBoolConst(r), true, r);
    }
    //   true  | X  =>  true
    //   false | X  =>  X
    if (m_isGround) return m_groundValue ? *this : other;
    if (other.m_isGround) return other.m_groundValue ? other : *this;
    return SMTBoolean(SMTFactory::makeBoolOr(m_expr, other.m_expr));
}

SMTBoolean SMTBoolean::operator^(const SMTBoolean& other) const {
    if (m_isGround && other.m_isGround) {
        bool r = m_groundValue != other.m_groundValue;
        return SMTBoolean(SMTFactory::makeBoolConst(r), true, r);
    }
    //   false ^ X  =>  X
    //   true  ^ X  =>  !X
    if (m_isGround) return m_groundValue ? other.negate() : other;
    if (other.m_isGround) return other.m_groundValue ? this->negate() : *this;
    return SMTBoolean(SMTFactory::makeBoolXor(m_expr, other.m_expr));
}

SMTBoolean SMTBoolean::negate() const {
    if (m_isGround) {
        bool r = !m_groundValue;
        return SMTBoolean(SMTFactory::makeBoolConst(r), true, r);
    }
    return SMTBoolean(SMTFactory::makeBoolNot(m_expr));
}

SMTBoolean SMTBoolean::ite(const SMTBoolean& thenB, const SMTBoolean& elseB) const {
    if (m_isGround) {
        return m_groundValue ? thenB : elseB;
    }
    return SMTBoolean(SMTFactory::makeIte(m_expr, thenB.m_expr, elseB.m_expr));
}

SMTNumber SMTBoolean::Int() const {
    int w = (int)iAbstractNumberLength;
    if (m_isGround) {
        uint64_t r = m_groundValue ? 1 : 0;
        return SMTNumber(r, w);
    }
    if (bSMTLogic == eLogicQF_LIA) {
        SMTExpr* zero = SMTFactory::makeIntConst(0);
        SMTExpr* one  = SMTFactory::makeIntConst(1);
        return SMTNumber(SMTFactory::makeIte(m_expr, one, zero));
    }
    SMTExpr* zero = SMTFactory::makeBvConst(0, w);
    SMTExpr* one  = SMTFactory::makeBvConst(1, w);
    return SMTNumber(SMTFactory::makeIte(m_expr, one, zero));
}

void SMTBoolean::print(ostream& out) const {
    m_expr->print(out);
}
