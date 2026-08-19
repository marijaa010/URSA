#include "SMTExpr.h"
#include <sstream>
#include <iomanip>
#include <unordered_map>
#include <unordered_set>
#include <string>

using namespace std;

namespace {

struct SMTExprHash {
    size_t operator()(const SMTExpr* e) const noexcept {
        size_t h = std::hash<int>{}(static_cast<int>(e->type));
        h ^= std::hash<int>{}(e->width) + 0x9e3779b9 + (h << 6) + (h >> 2);
        switch (e->type) {
            case BV_CONST:
            case INT_CONST:
                h ^= std::hash<uint64_t>{}(e->constValue) + 0x9e3779b9 + (h << 6) + (h >> 2);
                break;
            case BOOL_CONST:
                h ^= std::hash<bool>{}(e->boolValue) + 0x9e3779b9 + (h << 6) + (h >> 2);
                break;
            case BV_VAR:
            case BOOL_VAR:
            case INT_VAR:
            case ARRAY_VAR:
                h ^= std::hash<std::string>{}(e->varName) + 0x9e3779b9 + (h << 6) + (h >> 2);
                break;
            default:
                for (SMTExpr* ch : e->children) {
                    h ^= std::hash<const void*>{}(ch) + 0x9e3779b9 + (h << 6) + (h >> 2);
                }
        }
        return h;
    }
};

struct SMTExprEq {
    bool operator()(const SMTExpr* a, const SMTExpr* b) const noexcept {
        if (a == b) return true;
        if (a->type != b->type) return false;
        if (a->width != b->width) return false;
        switch (a->type) {
            case BV_CONST:
            case INT_CONST:   return a->constValue == b->constValue;
            case BOOL_CONST:  return a->boolValue == b->boolValue;
            case BV_VAR:
            case BOOL_VAR:
            case INT_VAR:     return a->varName == b->varName;
            case ARRAY_VAR:   return a->varName == b->varName
                                   && a->indexWidth == b->indexWidth
                                   && a->indexWidth2 == b->indexWidth2
                                   && a->is2D == b->is2D;
            default:
                if (a->children.size() != b->children.size()) return false;
                for (size_t i = 0; i < a->children.size(); i++)
                    if (a->children[i] != b->children[i]) return false;
                return true;
        }
    }
};

using SMTExprCache = std::unordered_set<SMTExpr*, SMTExprHash, SMTExprEq>;

SMTExprCache& cache() {
    static SMTExprCache c;
    return c;
}

SMTExpr* intern(SMTExpr* candidate) {
    auto& c = cache();
    auto it = c.find(candidate);
    if (it != c.end()) {
        delete candidate;
        return *it;
    }
    c.insert(candidate);
    return candidate;
}

}  // namespace


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

static void printIntConst(ostream& out, uint64_t value) {
    int64_t v = (int64_t)value;
    if (v < 0)
        out << "(- " << (uint64_t)(-v) << ")";
    else
        out << v;
}

static void printBinary(ostream& out, const char* op, const SMTExpr* e) {
    out << "(" << op << " ";
    e->children[0]->print(out);
    out << " ";
    e->children[1]->print(out);
    out << ")";
}

static void printTernary(ostream& out, const char* op, const SMTExpr* e) {
    out << "(" << op << " ";
    e->children[0]->print(out);
    out << " ";
    e->children[1]->print(out);
    out << " ";
    e->children[2]->print(out);
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

        case BV_UDIV:     printBinary(out, "bvudiv", this); break;
        case BV_UREM:     printBinary(out, "bvurem", this); break;

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

        case SMT_ITE:     printTernary(out, "ite", this); break;

        case INT_CONST:   printIntConst(out, constValue); break;
        case INT_VAR:     printSymbol(out, varName); break;
        case INT_ADD:     printBinary(out, "+", this); break;
        case INT_SUB:     printBinary(out, "-", this); break;
        case INT_MUL:     printBinary(out, "*", this); break;
        case INT_NEG:     printUnary(out, "-", this); break;
        case INT_DIV:     printBinary(out, "div", this); break;
        case INT_MOD:     printBinary(out, "mod", this); break;
        case INT_LT:      printBinary(out, "<", this); break;
        case INT_LE:      printBinary(out, "<=", this); break;
        case INT_GT:      printBinary(out, ">", this); break;
        case INT_GE:      printBinary(out, ">=", this); break;
        case INT_EQ:      printBinary(out, "=", this); break;

        case ARRAY_VAR:    printSymbol(out, varName); break;
        case ARRAY_SELECT: printBinary(out, "select", this); break;
        case ARRAY_STORE:  printTernary(out, "store", this); break;

        default:
            out << ";; UNKNOWN_TYPE";
            break;
    }
}

size_t SMTExpr::treeSize() const {
    size_t total = 1;
    for (const SMTExpr* ch : children) total += ch->treeSize();
    return total;
}

void SMTExpr::collectVarRefs(std::map<std::string, SMTExpr*>& out) const {
    if (type == BV_VAR || type == INT_VAR || type == BOOL_VAR) {
        out.insert({varName, const_cast<SMTExpr*>(this)});
    }
    for (const SMTExpr* ch : children) ch->collectVarRefs(out);
}

static void gatherAssociative(const SMTExpr* e, SMTNodeType op,
                              std::vector<const SMTExpr*>& out) {
    if (e->type == op) {
        for (const SMTExpr* ch : e->children)
            gatherAssociative(ch, op, out);
    } else {
        out.push_back(e);
    }
}

namespace {
struct EmitCtx {
    std::unordered_map<const SMTExpr*, std::string> letName;
    const std::string* lookup(const SMTExpr* e) const {
        auto it = letName.find(e);
        return (it == letName.end()) ? nullptr : &it->second;
    }
};
}  // namespace

static void emitCompact(std::ostream& out, const SMTExpr* e, const EmitCtx* ctx);
static void emitPretty(std::ostream& out, const SMTExpr* e, int indent,
                       const EmitCtx* ctx);

void SMTExpr::printPretty(std::ostream& out, int indent) const {
    emitPretty(out, this, indent, nullptr);
}

static void emitCompact(std::ostream& out, const SMTExpr* e, const EmitCtx* ctx) {
    if (ctx) {
        if (const std::string* nm = ctx->lookup(e)) { out << *nm; return; }
    }
    switch (e->type) {
        case BV_CONST:    out << SMTExpr::formatBvConst(e->constValue, e->width); return;
        case BV_VAR:      printSymbol(out, e->varName); return;
        case BOOL_CONST:  out << (e->boolValue ? "true" : "false"); return;
        case BOOL_VAR:    printSymbol(out, e->varName); return;
        case INT_CONST:   printIntConst(out, e->constValue); return;
        case INT_VAR:     printSymbol(out, e->varName); return;
        case ARRAY_VAR:   printSymbol(out, e->varName); return;
        default: break;
    }
    const char* op = nullptr;
    switch (e->type) {
        case BV_ADD:      op = "bvadd"; break;
        case BV_SUB:      op = "bvsub"; break;
        case BV_MUL:      op = "bvmul"; break;
        case BV_NEG:      op = "bvneg"; break;
        case BV_AND:      op = "bvand"; break;
        case BV_OR:       op = "bvor"; break;
        case BV_XOR:      op = "bvxor"; break;
        case BV_NOT:      op = "bvnot"; break;
        case BV_SHL:      op = "bvshl"; break;
        case BV_LSHR:     op = "bvlshr"; break;
        case BV_UDIV:     op = "bvudiv"; break;
        case BV_UREM:     op = "bvurem"; break;
        case BV_EQ:       op = "="; break;
        case BV_ULT:      op = "bvult"; break;
        case BV_ULE:      op = "bvule"; break;
        case BV_UGT:      op = "bvugt"; break;
        case BV_UGE:      op = "bvuge"; break;
        case BOOL_AND:    op = "and"; break;
        case BOOL_OR:     op = "or"; break;
        case BOOL_XOR:    op = "xor"; break;
        case BOOL_NOT:    op = "not"; break;
        case BOOL_EQ:     op = "="; break;
        case INT_ADD:     op = "+"; break;
        case INT_SUB:     op = "-"; break;
        case INT_MUL:     op = "*"; break;
        case INT_NEG:     op = "-"; break;
        case INT_DIV:     op = "div"; break;
        case INT_MOD:     op = "mod"; break;
        case INT_LT:      op = "<"; break;
        case INT_LE:      op = "<="; break;
        case INT_GT:      op = ">"; break;
        case INT_GE:      op = ">="; break;
        case INT_EQ:      op = "="; break;
        case ARRAY_SELECT: op = "select"; break;
        case ARRAY_STORE:  op = "store"; break;
        case SMT_ITE:     op = "ite"; break;
        default:          out << ";; UNKNOWN_TYPE"; return;
    }
    out << "(" << op;
    for (const SMTExpr* ch : e->children) {
        out << " ";
        emitCompact(out, ch, ctx);
    }
    out << ")";
}

static void emitPretty(std::ostream& out, const SMTExpr* e, int indent,
                       const EmitCtx* ctx) {
    if (ctx) {
        if (const std::string* nm = ctx->lookup(e)) { out << *nm; return; }
    }
    if (e->type == BOOL_AND || e->type == BOOL_OR || e->type == BOOL_XOR) {
        const char* op =
            (e->type == BOOL_AND) ? "and" :
            (e->type == BOOL_OR)  ? "or"  : "xor";
        std::vector<const SMTExpr*> args;
        gatherAssociative(e, e->type, args);
        out << "(" << op;
        std::string pad(indent + 2, ' ');
        for (const SMTExpr* a : args) {
            out << "\n" << pad;
            emitPretty(out, a, indent + 2, ctx);
        }
        out << ")";
        return;
    }
    emitCompact(out, e, ctx);
}

static bool isLeafForSharing(const SMTExpr* e) {
    return e->type == BV_CONST || e->type == BV_VAR ||
           e->type == BOOL_CONST || e->type == BOOL_VAR ||
           e->type == INT_CONST || e->type == INT_VAR ||
           e->type == ARRAY_VAR;
}

static void countReferences(const SMTExpr* root,
                            std::unordered_map<const SMTExpr*, size_t>& refs) {
    std::vector<const SMTExpr*> stack{root};
    while (!stack.empty()) {
        const SMTExpr* e = stack.back();
        stack.pop_back();
        refs[e]++;
        if (refs[e] == 1) {
            for (const SMTExpr* ch : e->children) stack.push_back(ch);
        }
    }
}

static void topoSort(const SMTExpr* e,
                     const std::unordered_set<const SMTExpr*>& shared,
                     std::unordered_set<const SMTExpr*>& visited,
                     std::vector<const SMTExpr*>& order) {
    if (visited.count(e)) return;
    visited.insert(e);
    for (const SMTExpr* ch : e->children) {
        topoSort(ch, shared, visited, order);
    }
    if (shared.count(e)) order.push_back(e);
}

void SMTExpr::printWithLet(std::ostream& out, int indent) const {
    std::unordered_map<const SMTExpr*, size_t> refs;
    countReferences(this, refs);

    std::unordered_set<const SMTExpr*> shared;
    for (auto& kv : refs) {
        if (kv.second >= 2 && !isLeafForSharing(kv.first)) shared.insert(kv.first);
    }

    if (shared.empty()) {
        emitPretty(out, this, indent, nullptr);
        return;
    }

    std::vector<const SMTExpr*> order;
    std::unordered_set<const SMTExpr*> visited;
    topoSort(this, shared, visited, order);

    EmitCtx ctx;
    for (size_t i = 0; i < order.size(); i++) {
        ctx.letName[order[i]] = "$s" + std::to_string(i);
    }

    int curIndent = indent;
    for (size_t i = 0; i < order.size(); i++) {
        const SMTExpr* def = order[i];
        std::string pad(curIndent, ' ');
        out << "(let ((" << ctx.letName[def] << " ";
        std::string myName = ctx.letName[def];
        ctx.letName.erase(def);
        emitCompact(out, def, &ctx);
        ctx.letName[def] = myName;
        out << "))\n" << pad << "  ";
        curIndent += 2;
    }

    emitPretty(out, this, curIndent, &ctx);

    for (size_t i = 0; i < order.size(); i++) out << ")";
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
    return intern(e);
}

static SMTExpr* makeUn(SMTNodeType t, SMTExpr* a, int width) {
    SMTExpr* e = new SMTExpr(t, width);
    e->children.push_back(a);
    return intern(e);
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
    return intern(e);
}

SMTExpr* SMTFactory::makeBvVar(const string& name, int width) {
    SMTExpr* e = new SMTExpr(BV_VAR, width);
    e->varName = name;
    return intern(e);
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
SMTExpr* SMTFactory::makeBvUdiv(SMTExpr* a, SMTExpr* b) { checkWidths("bvudiv", a, b); return makeBin(BV_UDIV, a, b, a->width); }
SMTExpr* SMTFactory::makeBvUrem(SMTExpr* a, SMTExpr* b) { checkWidths("bvurem", a, b); return makeBin(BV_UREM, a, b, a->width); }

SMTExpr* SMTFactory::makeBvEq(SMTExpr* a, SMTExpr* b)  { checkWidths("=",     a, b); return makeBin(BV_EQ,  a, b, 0); }
SMTExpr* SMTFactory::makeBvUlt(SMTExpr* a, SMTExpr* b) { checkWidths("bvult", a, b); return makeBin(BV_ULT, a, b, 0); }
SMTExpr* SMTFactory::makeBvUle(SMTExpr* a, SMTExpr* b) { checkWidths("bvule", a, b); return makeBin(BV_ULE, a, b, 0); }
SMTExpr* SMTFactory::makeBvUgt(SMTExpr* a, SMTExpr* b) { checkWidths("bvugt", a, b); return makeBin(BV_UGT, a, b, 0); }
SMTExpr* SMTFactory::makeBvUge(SMTExpr* a, SMTExpr* b) { checkWidths("bvuge", a, b); return makeBin(BV_UGE, a, b, 0); }

SMTExpr* SMTFactory::makeBoolConst(bool value) {
    SMTExpr* e = new SMTExpr(BOOL_CONST, 0);
    e->boolValue = value;
    return intern(e);
}
SMTExpr* SMTFactory::makeBoolVar(const string& name) {
    SMTExpr* e = new SMTExpr(BOOL_VAR, 0);
    e->varName = name;
    return intern(e);
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
    return intern(e);
}

SMTExpr* SMTFactory::makeIntConst(uint64_t value) {
    SMTExpr* e = new SMTExpr(INT_CONST, 0);
    e->constValue = value;
    return intern(e);
}
SMTExpr* SMTFactory::makeIntVar(const string& name) {
    SMTExpr* e = new SMTExpr(INT_VAR, 0);
    e->varName = name;
    return intern(e);
}
SMTExpr* SMTFactory::makeIntAdd(SMTExpr* a, SMTExpr* b) { return makeBin(INT_ADD, a, b, 0); }
SMTExpr* SMTFactory::makeIntSub(SMTExpr* a, SMTExpr* b) { return makeBin(INT_SUB, a, b, 0); }
SMTExpr* SMTFactory::makeIntMul(SMTExpr* a, SMTExpr* b) { return makeBin(INT_MUL, a, b, 0); }
SMTExpr* SMTFactory::makeIntNeg(SMTExpr* a)             { return makeUn(INT_NEG, a, 0); }
SMTExpr* SMTFactory::makeIntDiv(SMTExpr* a, SMTExpr* b) { return makeBin(INT_DIV, a, b, 0); }
SMTExpr* SMTFactory::makeIntMod(SMTExpr* a, SMTExpr* b) { return makeBin(INT_MOD, a, b, 0); }
SMTExpr* SMTFactory::makeIntLt(SMTExpr* a, SMTExpr* b)  { return makeBin(INT_LT, a, b, 0); }
SMTExpr* SMTFactory::makeIntLe(SMTExpr* a, SMTExpr* b)  { return makeBin(INT_LE, a, b, 0); }
SMTExpr* SMTFactory::makeIntGt(SMTExpr* a, SMTExpr* b)  { return makeBin(INT_GT, a, b, 0); }
SMTExpr* SMTFactory::makeIntGe(SMTExpr* a, SMTExpr* b)  { return makeBin(INT_GE, a, b, 0); }
SMTExpr* SMTFactory::makeIntEq(SMTExpr* a, SMTExpr* b)  { return makeBin(INT_EQ, a, b, 0); }

SMTExpr* SMTFactory::makeArrayVar(const string& name,
                                  int indexWidth, int elementWidth) {
    SMTExpr* e = new SMTExpr(ARRAY_VAR, elementWidth);
    e->varName = name;
    e->indexWidth = indexWidth;
    e->indexWidth2 = 0;
    e->is2D = false;
    return intern(e);
}

SMTExpr* SMTFactory::makeArrayVar2D(const string& name,
                                    int indexWidth1, int indexWidth2,
                                    int elementWidth) {
    SMTExpr* e = new SMTExpr(ARRAY_VAR, elementWidth);
    e->varName = name;
    e->indexWidth = indexWidth1;
    e->indexWidth2 = indexWidth2;
    e->is2D = true;
    return intern(e);
}

SMTExpr* SMTFactory::makeArraySelect(SMTExpr* array, SMTExpr* index) {
    SMTExpr* e = new SMTExpr(ARRAY_SELECT, array->width);
    e->children.push_back(array);
    e->children.push_back(index);
    return intern(e);
}

SMTExpr* SMTFactory::makeArrayStore(SMTExpr* array, SMTExpr* index, SMTExpr* value) {
    SMTExpr* e = new SMTExpr(ARRAY_STORE, array->width);
    e->children.push_back(array);
    e->children.push_back(index);
    e->children.push_back(value);
    return intern(e);
}

void SMTFactory::clear() {
    auto& c = cache();
    for (SMTExpr* e : c) delete e;
    c.clear();
}

size_t SMTFactory::cacheSize() {
    return cache().size();
}
