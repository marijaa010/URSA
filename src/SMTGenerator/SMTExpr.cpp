#include "SMTExpr.h"
#include <sstream>
#include <iomanip>
#include <unordered_map>
#include <unordered_set>
#include <string>

using namespace std;

// ---------------------------------------------------------------------------
// Hash-consing: structurally-identical SMTExpr nodes share a single instance.
// Analogous to URSA's FormulaFactory.existingFormulas for the SAT path.
//
// The intern() routine takes a freshly-allocated candidate node; if the cache
// already has a structurally-equal node, candidate is deleted and the cached
// pointer is returned. Otherwise the candidate is inserted and returned.
//
// Equality compares: type, width, plus the type-specific fields. For composite
// nodes, equality compares child *pointers* — which is safe precisely because
// all child nodes have themselves been interned (so equal subtrees share
// pointers).
// ---------------------------------------------------------------------------

namespace {

struct SMTExprHash {
    size_t operator()(const SMTExpr* e) const noexcept {
        size_t h = std::hash<int>{}(static_cast<int>(e->type));
        h ^= std::hash<int>{}(e->width) + 0x9e3779b9 + (h << 6) + (h >> 2);
        switch (e->type) {
            case BV_CONST:
                h ^= std::hash<uint64_t>{}(e->constValue) + 0x9e3779b9 + (h << 6) + (h >> 2);
                break;
            case BOOL_CONST:
                h ^= std::hash<bool>{}(e->boolValue) + 0x9e3779b9 + (h << 6) + (h >> 2);
                break;
            case BV_VAR:
            case BOOL_VAR:
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
            case BV_CONST:    return a->constValue == b->constValue;
            case BOOL_CONST:  return a->boolValue == b->boolValue;
            case BV_VAR:
            case BOOL_VAR:    return a->varName == b->varName;
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

size_t SMTExpr::treeSize() const {
    size_t total = 1;
    for (const SMTExpr* ch : children) total += ch->treeSize();
    return total;
}

// Collect the arguments of a nested associative operator by flattening the
// right-nested spine URSA builds through repeated binary `&`/`|` calls.
static void gatherAssociative(const SMTExpr* e, SMTNodeType op,
                              std::vector<const SMTExpr*>& out) {
    if (e->type == op) {
        for (const SMTExpr* ch : e->children)
            gatherAssociative(ch, op, out);
    } else {
        out.push_back(e);
    }
}

// ---------------------------------------------------------------------------
// Emit context: when non-null, tells the compact / pretty printers to emit
// a let-name instead of the full expression for shared subexpressions.
// ---------------------------------------------------------------------------

namespace {
struct EmitCtx {
    std::unordered_map<const SMTExpr*, std::string> letName;
    const std::string* lookup(const SMTExpr* e) const {
        auto it = letName.find(e);
        return (it == letName.end()) ? nullptr : &it->second;
    }
};
}  // namespace

// Forward declarations for context-aware emitters (defined further below).
static void emitCompact(std::ostream& out, const SMTExpr* e, const EmitCtx* ctx);
static void emitPretty(std::ostream& out, const SMTExpr* e, int indent,
                       const EmitCtx* ctx);

void SMTExpr::printPretty(std::ostream& out, int indent) const {
    emitPretty(out, this, indent, /*ctx=*/nullptr);
}

// Compact emitter — same as SMTExpr::print but honors ctx (emits `s7` for
// a let-bound node instead of its full expression).
static void emitCompact(std::ostream& out, const SMTExpr* e, const EmitCtx* ctx) {
    if (ctx) {
        if (const std::string* nm = ctx->lookup(e)) { out << *nm; return; }
    }
    // Reproduce the logic of SMTExpr::print but recursing through emitCompact
    // so nested children also honor ctx.
    switch (e->type) {
        case BV_CONST:    out << SMTExpr::formatBvConst(e->constValue, e->width); return;
        case BV_VAR:      printSymbol(out, e->varName); return;
        case BOOL_CONST:  out << (e->boolValue ? "true" : "false"); return;
        case BOOL_VAR:    printSymbol(out, e->varName); return;
        default: break;
    }
    // Everything else is (op child*) with 1..3 children.
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

// Pretty (multiline) emitter — flatten associative AND/OR/XOR spines and put
// each conjunct on its own line. All other constructors fall back to compact.
static void emitPretty(std::ostream& out, const SMTExpr* e, int indent,
                       const EmitCtx* ctx) {
    // If this node is a let-bound name in the current context, honor that
    // before doing anything else.
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

// ---------------------------------------------------------------------------
// Let-binding factorization: find shared subexpressions and print
// `(let ((s0 e0)) (let ((s1 e1)) ... body))` where e_i may reference s_j (j<i).
// ---------------------------------------------------------------------------

static bool isLeafForSharing(const SMTExpr* e) {
    // These are already atomic in SMT-LIB — naming them saves no space.
    return e->type == BV_CONST || e->type == BV_VAR ||
           e->type == BOOL_CONST || e->type == BOOL_VAR;
}

// Count how many times each subexpression is referenced when walking `root`.
// Uses the shared-pointer identity established by hash-consing.
static void countReferences(const SMTExpr* root,
                            std::unordered_map<const SMTExpr*, size_t>& refs) {
    // Iterative DFS to avoid stack overflow on deep URSA formulas.
    std::vector<const SMTExpr*> stack{root};
    while (!stack.empty()) {
        const SMTExpr* e = stack.back();
        stack.pop_back();
        refs[e]++;
        // Only descend once — subsequent occurrences just bump the counter.
        if (refs[e] == 1) {
            for (const SMTExpr* ch : e->children) stack.push_back(ch);
        }
    }
}

// Topological order: children before parents. Ensures a name may reference
// earlier names in the nested let chain.
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
    // Step 1: count references. Only composite nodes with count >= 2 are
    // interesting candidates for let-binding.
    std::unordered_map<const SMTExpr*, size_t> refs;
    countReferences(this, refs);

    std::unordered_set<const SMTExpr*> shared;
    for (auto& kv : refs) {
        if (kv.second >= 2 && !isLeafForSharing(kv.first)) shared.insert(kv.first);
    }

    if (shared.empty()) {
        emitPretty(out, this, indent, /*ctx=*/nullptr);
        return;
    }

    // Step 2: topological sort so `s1 = (op s0 ...)` sees s0 already defined.
    std::vector<const SMTExpr*> order;
    std::unordered_set<const SMTExpr*> visited;
    topoSort(this, shared, visited, order);

    // Step 3: assign names.
    EmitCtx ctx;
    for (size_t i = 0; i < order.size(); i++) {
        ctx.letName[order[i]] = "$s" + std::to_string(i);
    }

    // Step 4: emit nested lets. Each level increases the indent by 2.
    int curIndent = indent;
    for (size_t i = 0; i < order.size(); i++) {
        const SMTExpr* def = order[i];
        std::string pad(curIndent, ' ');
        out << "(let ((" << ctx.letName[def] << " ";
        // Emit the definition WITHOUT the current node's own name (avoid
        // self-reference), but WITH names of previously-defined ones. We do
        // this by temporarily removing this node's entry before recursing.
        std::string myName = ctx.letName[def];
        ctx.letName.erase(def);
        emitCompact(out, def, &ctx);
        ctx.letName[def] = myName;
        out << "))\n" << pad << "  ";
        curIndent += 2;
    }

    // Step 5: emit the body with all names active. Break lines for AND/OR.
    emitPretty(out, this, curIndent, &ctx);

    // Step 6: close all the opened parentheses.
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

void SMTFactory::clear() {
    auto& c = cache();
    for (SMTExpr* e : c) delete e;
    c.clear();
}

size_t SMTFactory::cacheSize() {
    return cache().size();
}
