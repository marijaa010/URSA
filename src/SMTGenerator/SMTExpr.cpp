#include "SMTExpr.h"
#include <sstream>
#include <iomanip>

using namespace std;


static bool needsQuoting(const string& s) {
    if (s.empty()) return true;
    for (char c : s) {
        if (c >= 'a' && c <= 'z') continue;
        if (c >= 'A' && c <= 'Z') continue;
        if (c >= '0' && c <= '9') continue;
        if (c == '+' || c == '-' || c == '/' || c == '*' || c == '=' ||
            c == '%' || c == '?' || c == '!' || c == '.' || c == '$' ||
            c == '_' || c == '~' || c == '&' || c == '^' || c == '<' ||
            c == '>' || c == '@') continue;
        return true;
    }
    return false;
}

static void printSymbol(ostream& out, const string& name) {
    if (needsQuoting(name))
        out << "|" << name << "|";
    else
        out << name;
}

static void printBinary(ostream& out, const char* op, const SMTExpr* e) {
    out << "(" << op << " ";
    e->children[0]->print(out);
    out << " ";
    e->children[1]->print(out);
    out << ")";
}

static void printUnary(ostream& out, const char* op, const SMTExpr* e) {
    out << "(" << op << " ";
    e->children[0]->print(out);
    out << ")";
}

void SMTExpr::print(ostream& out) const {
    switch (type) {
        case BV_CONST:    out << formatBvConst(constValue, width); break;
        case BV_VAR:      printSymbol(out, varName); break;

        case BV_ADD:      printBinary(out, "bvadd", this); break;
        case BV_SUB:      printBinary(out, "bvsub", this); break;
        case BV_MUL:      printBinary(out, "bvmul", this); break;
        case BV_NEG:      printUnary(out, "bvneg", this); break;

        case BV_AND:      printBinary(out, "bvand", this); break;
        case BV_OR:       printBinary(out, "bvor", this); break;
        case BV_XOR:      printBinary(out, "bvxor", this); break;
        case BV_NOT:      printUnary(out, "bvnot", this); break;

        case BV_SHL:      printBinary(out, "bvshl", this); break;
        case BV_LSHR:     printBinary(out, "bvlshr", this); break;

        case BV_EQ:       printBinary(out, "=", this); break;
        case BV_ULT:      printBinary(out, "bvult", this); break;
        case BV_ULE:      printBinary(out, "bvule", this); break;
        case BV_UGT:      printBinary(out, "bvugt", this); break;
        case BV_UGE:      printBinary(out, "bvuge", this); break;

        case BOOL_CONST:  out << (boolValue ? "true" : "false"); break;
        case BOOL_VAR:    printSymbol(out, varName); break;
        case BOOL_AND:    printBinary(out, "and", this); break;
        case BOOL_OR:     printBinary(out, "or", this); break;
        case BOOL_XOR:    printBinary(out, "xor", this); break;
        case BOOL_NOT:    printUnary(out, "not", this); break;
        case BOOL_EQ:     printBinary(out, "=", this); break;

        case SMT_ITE:
            out << "(ite ";
            children[0]->print(out);
            out << " ";
            children[1]->print(out);
            out << " ";
            children[2]->print(out);
            out << ")";
            break;

        default:
            out << ";; UNKNOWN_TYPE";
            break;
    }
}

string SMTExpr::formatBvConst(uint64_t value, int width) {
    stringstream ss;
    if (width % 4 == 0) {
        ss << "#x";
        ss << hex << setw(width / 4) << setfill('0') << value;
    } else {
        ss << "#b";
        for (int i = width - 1; i >= 0; i--) {
            ss << ((value >> i) & 1);
        }
    }
    return ss.str();
}

static SMTExpr* makeBin(SMTNodeType t, SMTExpr* a, SMTExpr* b, int width) {
    SMTExpr* e = new SMTExpr(t, width);
    e->children.push_back(a);
    e->children.push_back(b);
    return e;
}

static SMTExpr* makeUn(SMTNodeType t, SMTExpr* a, int width) {
    SMTExpr* e = new SMTExpr(t, width);
    e->children.push_back(a);
    return e;
}

static void checkWidths(const char* op, SMTExpr* a, SMTExpr* b) {
    if (a->width != b->width) {
        cerr << "ERROR: " << op << " width mismatch: "
             << a->width << " vs " << b->width << endl;
    }
}

SMTExpr* SMTFactory::makeBvConst(uint64_t value, int width) {
    SMTExpr* e = new SMTExpr(BV_CONST, width);
    uint64_t mask = (width >= 64) ? ~0ULL : ((1ULL << width) - 1);
    e->constValue = value & mask;
    return e;
}

SMTExpr* SMTFactory::makeBvVar(const string& name, int width) {
    SMTExpr* e = new SMTExpr(BV_VAR, width);
    e->varName = name;
    return e;
}

SMTExpr* SMTFactory::makeBvAdd(SMTExpr* a, SMTExpr* b) { checkWidths("bvadd", a, b); return makeBin(BV_ADD, a, b, a->width); }
SMTExpr* SMTFactory::makeBvSub(SMTExpr* a, SMTExpr* b) { checkWidths("bvsub", a, b); return makeBin(BV_SUB, a, b, a->width); }
SMTExpr* SMTFactory::makeBvMul(SMTExpr* a, SMTExpr* b) { checkWidths("bvmul", a, b); return makeBin(BV_MUL, a, b, a->width); }
SMTExpr* SMTFactory::makeBvNeg(SMTExpr* a) { return makeUn(BV_NEG, a, a->width); }

SMTExpr* SMTFactory::makeBvAnd(SMTExpr* a, SMTExpr* b) { checkWidths("bvand", a, b); return makeBin(BV_AND, a, b, a->width); }
SMTExpr* SMTFactory::makeBvOr(SMTExpr* a, SMTExpr* b)  { checkWidths("bvor",  a, b); return makeBin(BV_OR,  a, b, a->width); }
SMTExpr* SMTFactory::makeBvXor(SMTExpr* a, SMTExpr* b) { checkWidths("bvxor", a, b); return makeBin(BV_XOR, a, b, a->width); }
SMTExpr* SMTFactory::makeBvNot(SMTExpr* a) { return makeUn(BV_NOT, a, a->width); }

SMTExpr* SMTFactory::makeBvShl(SMTExpr* a, SMTExpr* b)  { checkWidths("bvshl",  a, b); return makeBin(BV_SHL,  a, b, a->width); }
SMTExpr* SMTFactory::makeBvLshr(SMTExpr* a, SMTExpr* b) { checkWidths("bvlshr", a, b); return makeBin(BV_LSHR, a, b, a->width); }

SMTExpr* SMTFactory::makeBvEq(SMTExpr* a, SMTExpr* b)  { checkWidths("=",     a, b); return makeBin(BV_EQ,  a, b, 0); }
SMTExpr* SMTFactory::makeBvUlt(SMTExpr* a, SMTExpr* b) { checkWidths("bvult", a, b); return makeBin(BV_ULT, a, b, 0); }
SMTExpr* SMTFactory::makeBvUle(SMTExpr* a, SMTExpr* b) { checkWidths("bvule", a, b); return makeBin(BV_ULE, a, b, 0); }
SMTExpr* SMTFactory::makeBvUgt(SMTExpr* a, SMTExpr* b) { checkWidths("bvugt", a, b); return makeBin(BV_UGT, a, b, 0); }
SMTExpr* SMTFactory::makeBvUge(SMTExpr* a, SMTExpr* b) { checkWidths("bvuge", a, b); return makeBin(BV_UGE, a, b, 0); }

SMTExpr* SMTFactory::makeBoolConst(bool value) {
    SMTExpr* e = new SMTExpr(BOOL_CONST, 0);
    e->boolValue = value;
    return e;
}
SMTExpr* SMTFactory::makeBoolVar(const string& name) {
    SMTExpr* e = new SMTExpr(BOOL_VAR, 0);
    e->varName = name;
    return e;
}
SMTExpr* SMTFactory::makeBoolAnd(SMTExpr* a, SMTExpr* b) { return makeBin(BOOL_AND, a, b, 0); }
SMTExpr* SMTFactory::makeBoolOr(SMTExpr* a, SMTExpr* b)  { return makeBin(BOOL_OR,  a, b, 0); }
SMTExpr* SMTFactory::makeBoolXor(SMTExpr* a, SMTExpr* b) { return makeBin(BOOL_XOR, a, b, 0); }
SMTExpr* SMTFactory::makeBoolNot(SMTExpr* a)             { return makeUn(BOOL_NOT, a, 0); }
SMTExpr* SMTFactory::makeBoolEq(SMTExpr* a, SMTExpr* b)  { return makeBin(BOOL_EQ, a, b, 0); }

SMTExpr* SMTFactory::makeIte(SMTExpr* cond, SMTExpr* thenE, SMTExpr* elseE) {
    SMTExpr* e = new SMTExpr(SMT_ITE, thenE->width);
    e->children.push_back(cond);
    e->children.push_back(thenE);
    e->children.push_back(elseE);
    return e;
}
