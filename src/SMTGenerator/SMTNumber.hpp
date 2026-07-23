#ifndef __SMT_NUMBER_H
#define __SMT_NUMBER_H

#include "SMTExpr.h"
#include <string>
#include <cstdint>

class SMTBoolean;

class SMTNumber {
public:
    SMTNumber();
    SMTNumber(int width);
    SMTNumber(uint64_t value, int width);
    SMTNumber(const std::string& varName, int width);
    SMTNumber(SMTExpr* expr, bool isGround = false, uint64_t groundVal = 0);

    SMTExpr* getExpr() const { return m_expr; }
    int getWidth() const { return m_width; }

    bool IsGroundNumber() const { return m_isGround; }
    uint64_t GetGroundValueUnsigned() const;

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

    SMTNumber negate() const;
    SMTNumber bitnegate() const;
    SMTNumber sgn() const;

    SMTBoolean operator==(const SMTNumber& other) const;
    SMTBoolean operator!=(const SMTNumber& other) const;
    SMTBoolean operator<(const SMTNumber& other) const;
    SMTBoolean operator>(const SMTNumber& other) const;
    SMTBoolean operator<=(const SMTNumber& other) const;
    SMTBoolean operator>=(const SMTNumber& other) const;

    SMTNumber ite(const SMTBoolean& cond, const SMTNumber& other) const;
    SMTBoolean Bool() const;

    void print(std::ostream& out) const;

private:
    SMTExpr* m_expr;
    int m_width;
    bool m_isGround;
    uint64_t m_groundValue;

    static uint64_t maskTo(uint64_t v, int width);
};

#endif
