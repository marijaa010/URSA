#include <iostream>
#include <vector>
#include <set>
#include "SMTExpr.h"
#include "SMTNumber.hpp"
#include "SMTBoolean.hpp"

using namespace std;

unsigned int iAbstractNumberLength = 8;

struct VarDecl {
    string name;
    int width;
};
vector<VarDecl> declarations;

vector<SMTExpr*> assertions;


SMTNumber makeUnknown(const string& name, int width) {
    declarations.push_back({name, width});
    return SMTNumber(name, width);
}

void addAssertion(const SMTBoolean& b) {
    assertions.push_back(b.getExpr());
}


void emitSMTLib(ostream& out) {
    out << "(set-logic QF_BV)" << endl;
    out << endl;

    for (const auto& d : declarations) {
        out << "(declare-fun " << d.name
            << " () (_ BitVec " << d.width << "))" << endl;
    }
    out << endl;

    for (auto* a : assertions) {
        out << "(assert ";
        a->print(out);
        out << ")" << endl;
    }
    out << endl;

    out << "(check-sat)" << endl;
    out << "(get-model)" << endl;
}

int main() {
    const int WIDTH = 8;

    SMTNumber nA = makeUnknown("nA", WIDTH);

    SMTNumber one(1, WIDTH);
    SMTNumber nB = nA + one;

    SMTNumber two(2, WIDTH);
    SMTBoolean constraint = (nB == two);
    addAssertion(constraint);

    emitSMTLib(cout);

    return 0;
}
