#ifndef __SMT_EXPR_H
#define __SMT_EXPR_H

#include <string>
#include <vector>
#include <iostream>
#include <cstdint>

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

class SMTExpr {
public:
    SMTNodeType type;
    int width;
    std::vector<SMTExpr*> children;

    std::string varName;
    uint64_t constValue;
    bool boolValue;

    int indexWidth;
    int indexWidth2;
    bool is2D;

    SMTExpr(SMTNodeType t, int w)
        : type(t), width(w), constValue(0), boolValue(false),
          indexWidth(0), indexWidth2(0), is2D(false) {}

    void print(std::ostream& out) const;
    void printPretty(std::ostream& out, int indent = 0) const;
    void printWithLet(std::ostream& out, int indent = 0) const;
    size_t treeSize() const;

    static std::string formatBvConst(uint64_t value, int width);
};

class SMTFactory {
public:
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

    static SMTExpr* makeBoolConst(bool value);
    static SMTExpr* makeBoolVar(const std::string& name);
    static SMTExpr* makeBoolAnd(SMTExpr* a, SMTExpr* b);
    static SMTExpr* makeBoolOr(SMTExpr* a, SMTExpr* b);
    static SMTExpr* makeBoolXor(SMTExpr* a, SMTExpr* b);
    static SMTExpr* makeBoolNot(SMTExpr* a);
    static SMTExpr* makeBoolEq(SMTExpr* a, SMTExpr* b);

    static SMTExpr* makeIte(SMTExpr* cond, SMTExpr* thenE, SMTExpr* elseE);

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

    static SMTExpr* makeArrayVar(const std::string& name,
                                 int indexWidth, int elementWidth);
    static SMTExpr* makeArrayVar2D(const std::string& name,
                                   int indexWidth1, int indexWidth2,
                                   int elementWidth);
    static SMTExpr* makeArraySelect(SMTExpr* array, SMTExpr* index);
    static SMTExpr* makeArrayStore(SMTExpr* array, SMTExpr* index, SMTExpr* value);

    static void clear();
    static size_t cacheSize();
};

#endif
