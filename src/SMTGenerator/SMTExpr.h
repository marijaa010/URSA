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

    SMTExpr(SMTNodeType t, int w)
        : type(t), width(w), constValue(0), boolValue(false) {}

    void print(std::ostream& out) const;

    // Multiline pretty-print with flattening of associative AND/OR/XOR chains.
    // Useful as top-level printer for (assert ...) — makes the output readable
    // instead of a single huge line. `indent` is the number of spaces prepended
    // to the *inner* lines (the caller places the outer opening).
    void printPretty(std::ostream& out, int indent = 0) const;

    // Pretty-print with automatic let-bindings for shared subexpressions.
    // Requires that hash-consing is already active (which it is by default);
    // shared subexpressions are detected via pointer identity.
    // A composite node (not BV_CONST / *_VAR / BOOL_CONST) that appears more
    // than once inside this subtree is factored out into a `(let ((s N expr)) …)`
    // scope; lets are nested bottom-up so a name may refer to an earlier one.
    void printWithLet(std::ostream& out, int indent = 0) const;

    // Number of nodes in this subtree when traversed without sharing, i.e.
    // counting each occurrence separately. Useful for measuring how much
    // hash-consing saves: compare against SMTFactory::cacheSize().
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

    // Release all hash-consed nodes. Analogous to FormulaFactory::Clear() in
    // the SAT path. Call this at the end of an SMT session if you want to
    // free the DAG memory; otherwise the cache persists for process lifetime.
    static void clear();
    static size_t cacheSize();
};

#endif
