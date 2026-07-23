#ifndef __SMT_BOOLEAN_H
#define __SMT_BOOLEAN_H

#include "SMTExpr.h"
#include <iostream>

class SMTNumber;

class SMTBoolean {
public:
    SMTBoolean();
    SMTBoolean(bool value);
    SMTBoolean(SMTExpr* expr, bool isGround = false, bool groundVal = false);

    SMTExpr* getExpr() const { return m_expr; }

    bool IsGroundBoolean() const { return m_isGround; }
    bool GetGroundValue() const;

    SMTBoolean operator&(const SMTBoolean& other) const;
    SMTBoolean operator|(const SMTBoolean& other) const;
    SMTBoolean operator^(const SMTBoolean& other) const;
    SMTBoolean negate() const;

    SMTBoolean ite(const SMTBoolean& thenB, const SMTBoolean& elseB) const;
    SMTNumber Int() const;

    void print(std::ostream& out) const;

private:
    SMTExpr* m_expr;
    bool m_isGround;
    bool m_groundValue;
};

#endif
