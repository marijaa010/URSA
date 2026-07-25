#include "SMTSolverDriver.hpp"

#include <iostream>
#include <sstream>
#include <cstring>
#include <regex>
#include <unistd.h>
#include <sys/wait.h>

using namespace std;

static string unflattenArrayName(const string& name) {
    static const regex r2d(R"(^(.+?)_(\d+)__(\d+)_$)");
    static const regex r1d(R"(^(.+?)_(\d+)_$)");
    smatch m;
    if (regex_match(name, m, r2d))
        return m[1].str() + "[" + m[2].str() + "][" + m[3].str() + "]";
    if (regex_match(name, m, r1d))
        return m[1].str() + "[" + m[2].str() + "]";
    return name;
}

int SMTSolverDriver::run() {
    if (m_buffer.find("(declare-fun") == string::npos) {
        printTrivial();
        return 0;
    }

    if (m_hasOptimize && !m_backend.supportsOptimize()) {
        cerr << "ERROR: solver '" << m_backend.name()
             << "' does not support (minimize)/(maximize)." << endl
             << "       Use -smtsolve=z3 for programs with minimize/maximize,"
             << endl
             << "       or use -smt to emit SMT-LIB and drive a solver manually."
             << endl;
        return 1;
    }

    vector<FreeVar> freeVars = extractFreeVars();

    int solverIn, solverOut;
    pid_t childPid;
    if (!spawnSolver(solverIn, solverOut, childPid)) {
        return 1;
    }

    if (!writeAll(solverIn, m_buffer)) {
        cerr << "ERROR: could not write SMT-LIB to solver." << endl;
        close(solverIn); close(solverOut);
        waitpid(childPid, nullptr, 0);
        return 1;
    }

    int solutionCount = 0;
    while (true) {
        if (!writeAll(solverIn, "(check-sat)\n")) break;
        string firstLine = readLine(solverOut);
        if (firstLine == "unsat") break;
        if (firstLine == "unknown") {
            cerr << "Solver returned 'unknown' - could not determine satisfiability."
                 << endl;
            break;
        }
        if (firstLine != "sat") {
            if (!firstLine.empty())
                cerr << "Unexpected solver output: " << firstLine << endl;
            break;
        }

        if (m_hasOptimize) {
            writeAll(solverIn, "(get-objectives)\n");
            cout << "Objectives: " << readBalanced(solverOut) << endl;
        }

        vector<Value> values;
        if (!freeVars.empty()) {
            if (!writeAll(solverIn, buildGetValueCommand(freeVars))) break;
            string response = readBalanced(solverOut);
            values = parseGetValueResponse(response);
        }

        solutionCount++;
        printSolution(solutionCount, values);

        if (!m_assertAll || m_hasOptimize) break;

        if (!writeAll(solverIn, buildBlockingClause(values))) break;
    }

    writeAll(solverIn, "(exit)\n");
    close(solverIn);
    close(solverOut);
    waitpid(childPid, nullptr, 0);

    if (solutionCount == 0) {
        cout << "No solutions found." << endl;
    } else if (m_assertAll) {
        cerr << "[Number of solutions: " << solutionCount << "]" << endl;
    }
    return 0;
}

vector<SMTSolverDriver::FreeVar> SMTSolverDriver::extractFreeVars() const {
    vector<FreeVar> result;
    istringstream iss(m_buffer);
    string line;
    while (getline(iss, line)) {
        if (line.compare(0, 13, "(declare-fun ") != 0) continue;
        size_t nameStart = 13;
        size_t nameEnd = line.find(' ', nameStart);
        if (nameEnd == string::npos) continue;
        string name = line.substr(nameStart, nameEnd - nameStart);
        size_t sortStart = line.find("()", nameEnd);
        if (sortStart == string::npos) continue;
        sortStart += 2;
        while (sortStart < line.size() && line[sortStart] == ' ') sortStart++;
        size_t sortEnd = line.size();
        while (sortEnd > sortStart && line[sortEnd - 1] != ')') sortEnd--;
        if (sortEnd == sortStart) continue;
        string sort = line.substr(sortStart, sortEnd - sortStart - 1);
        if (sort.find("Array") != string::npos) continue;
        result.push_back(FreeVar{name, sort});
    }
    return result;
}

bool SMTSolverDriver::spawnSolver(int& solverIn, int& solverOut, pid_t& childPid) {
    int inPipe[2], outPipe[2];
    if (pipe(inPipe) < 0 || pipe(outPipe) < 0) {
        cerr << "ERROR: pipe() failed." << endl;
        return false;
    }
    childPid = fork();
    if (childPid < 0) {
        cerr << "ERROR: fork() failed." << endl;
        return false;
    }
    if (childPid == 0) {
        dup2(inPipe[0], STDIN_FILENO);
        dup2(outPipe[1], STDOUT_FILENO);
        close(inPipe[0]); close(inPipe[1]);
        close(outPipe[0]); close(outPipe[1]);

        string binary = m_backend.resolveBinary();
        vector<string> extras = m_backend.extraArgs();

        vector<char*> argv;
        argv.push_back(const_cast<char*>(binary.c_str()));
        for (auto& e : extras) argv.push_back(const_cast<char*>(e.c_str()));
        argv.push_back(nullptr);

        execvp(binary.c_str(), argv.data());
        _exit(127);
    }
    close(inPipe[0]);
    close(outPipe[1]);
    solverIn = inPipe[1];
    solverOut = outPipe[0];
    return true;
}

bool SMTSolverDriver::writeAll(int fd, const string& s) {
    const char* p = s.data();
    size_t left = s.size();
    while (left > 0) {
        ssize_t w = write(fd, p, left);
        if (w <= 0) return false;
        p += w;
        left -= (size_t)w;
    }
    return true;
}

string SMTSolverDriver::readLine(int fd) {
    string line;
    char c;
    while (read(fd, &c, 1) == 1) {
        if (c == '\n') {
            if (line.empty()) continue;
            return line;
        }
        if (c != '\r') line += c;
    }
    return line;
}

string SMTSolverDriver::readBalanced(int fd) {
    string s;
    int depth = 0;
    bool started = false;
    char c;
    while (read(fd, &c, 1) == 1) {
        if (!started) {
            if (c == '(') { started = true; depth = 1; s += c; }
            continue;
        }
        s += c;
        if (c == '(') depth++;
        else if (c == ')') {
            depth--;
            if (depth == 0) return s;
        }
    }
    return s;
}

vector<SMTSolverDriver::Value>
SMTSolverDriver::parseGetValueResponse(const string& s) {
    vector<Value> out;
    auto skipWs = [](const string& str, size_t& k) {
        while (k < str.size() &&
               (str[k]==' '||str[k]=='\n'||str[k]=='\t'||str[k]=='\r'))
            k++;
    };
    size_t i = 0;
    skipWs(s, i);
    if (i >= s.size() || s[i] != '(') return out;
    i++;
    while (true) {
        skipWs(s, i);
        if (i >= s.size() || s[i] == ')') break;
        if (s[i] != '(') { i++; continue; }
        i++;
        skipWs(s, i);
        size_t nameStart = i;
        while (i < s.size() &&
               s[i]!=' ' && s[i]!='\t' && s[i]!='\n' && s[i]!=')')
            i++;
        string name = s.substr(nameStart, i - nameStart);
        skipWs(s, i);
        string val;
        if (i < s.size() && s[i] == '(') {
            int d = 1;
            val += s[i++];
            while (i < s.size() && d > 0) {
                if (s[i] == '(') d++;
                else if (s[i] == ')') d--;
                val += s[i++];
            }
        } else {
            while (i < s.size() && s[i]!=' ' && s[i]!=')' && s[i]!='\n')
                val += s[i++];
        }
        out.push_back(Value{name, val});
        skipWs(s, i);
        while (i < s.size() && s[i] != ')') i++;
        if (i < s.size()) i++;
    }
    return out;
}

string SMTSolverDriver::toDecimal(const string& v) {
    if (v.size() >= 2 && v[0] == '#' && (v[1] == 'x' || v[1] == 'X')) {
        unsigned long long n = 0;
        for (size_t k = 2; k < v.size(); k++) {
            char c = v[k];
            int d;
            if (c >= '0' && c <= '9') d = c - '0';
            else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
            else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
            else break;
            n = n * 16 + d;
        }
        return to_string(n);
    }
    if (v.size() >= 2 && v[0] == '#' && (v[1] == 'b' || v[1] == 'B')) {
        unsigned long long n = 0;
        for (size_t k = 2; k < v.size(); k++) {
            if (v[k] != '0' && v[k] != '1') break;
            n = n * 2 + (v[k] - '0');
        }
        return to_string(n);
    }
    if (v.size() > 3 && v[0] == '(' && v[1] == '-') {
        size_t start = 2;
        while (start < v.size() && v[start] == ' ') start++;
        size_t end = v.find(')', start);
        if (end != string::npos) return "-" + v.substr(start, end - start);
    }
    return v;
}

string SMTSolverDriver::buildGetValueCommand(const vector<FreeVar>& vars) {
    string cmd = "(get-value (";
    for (size_t k = 0; k < vars.size(); k++) {
        if (k) cmd += " ";
        cmd += vars[k].name;
    }
    cmd += "))\n";
    return cmd;
}

string SMTSolverDriver::buildBlockingClause(const vector<Value>& values) {
    string block = "(assert (not (and";
    for (size_t k = 0; k < values.size(); k++) {
        block += " (= " + values[k].name + " " + values[k].rawValue + ")";
    }
    block += ")))\n";
    return block;
}

void SMTSolverDriver::printSolution(int solutionNumber,
                                    const vector<Value>& values) const {
    if (m_assertAll) cout << "--> Solution " << solutionNumber << endl;
    for (size_t k = 0; k < values.size(); k++) {
        cout << unflattenArrayName(values[k].name) << "="
             << toDecimal(values[k].rawValue) << ";" << endl;
    }
    if (m_assertAll) cout << endl;
}

void SMTSolverDriver::printTrivial() const {
    cout << m_buffer;
}
