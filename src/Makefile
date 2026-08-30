SHELL = /bin/sh
CXX = g++
LDFLAGS = -static-libstdc++
CXXFLAGS = -std=c++11 -O2 -DNDEBUG
ADDFLAGS ?=

# URSA can link Z3 and/or cvc5 as libraries and solve in-process
# (-smtsolve=z3|cvc5). Each solver is optional and selected at build time:
#   make                              # both (default)
#   make CVC5_SUPPORT=0               # Z3 only
#   make Z3_SUPPORT=0                 # cvc5 only
#   make Z3_SUPPORT=0 CVC5_SUPPORT=0  # no in-process solving (-smt still emits SMT-LIB)
# Prefixes may be overridden, e.g. make CVC5_PREFIX=/opt/cvc5. Z3 is
# auto-detected via Homebrew; both default to /usr/local otherwise.
# The cvc5 C++ headers require C++17, so only SMTSolverDriver.o is built with
# -std=c++17 (see explicit rule below); the rest of URSA stays C++11.
Z3_SUPPORT ?= 1
CVC5_SUPPORT ?= 1
SMT_INCLUDE =
SMT_DEFINES =

ifeq ($(Z3_SUPPORT),1)
Z3_PREFIX ?= $(shell brew --prefix z3 2>/dev/null || echo /usr/local)
SMT_INCLUDE += -I$(Z3_PREFIX)/include
SMT_DEFINES += -DZ3_SUPPORT
LDFLAGS += -L$(Z3_PREFIX)/lib -lz3
endif

ifeq ($(CVC5_SUPPORT),1)
CVC5_PREFIX ?= /usr/local
SMT_INCLUDE += -I$(CVC5_PREFIX)/include
SMT_DEFINES += -DCVC5_SUPPORT
LDFLAGS += -L$(CVC5_PREFIX)/lib -lcvc5parser -lcvc5 -lcadical -lcln -lglpk \
           -lpicpoly -lpicpolyxx -lcocoa -lgmpxx -lgmp
endif

# GMP provides arbitrary-precision integers for QF_LIA, so integer literals
# and folded constants are not truncated to 64 bits. On by default; with
# GMP_SUPPORT=0 the SAT and QF_BV paths still build and QF_LIA is refused.
GMP_SUPPORT ?= 1
GMP_FLAGS =
ifeq ($(GMP_SUPPORT),1)
GMP_PREFIX ?= $(shell brew --prefix gmp 2>/dev/null || echo /usr/local)
GMP_FLAGS = -DGMP_SUPPORT -I$(GMP_PREFIX)/include
# When cvc5 is linked it already provides GMP; otherwise link it here.
ifneq ($(CVC5_SUPPORT),1)
LDFLAGS += -L$(GMP_PREFIX)/lib -lgmpxx -lgmp
endif
endif

INCLUDE =-IAbstractNumbers -IParser -IInterpreter -IUtils -ISMTGenerator -ISATsolvers -ISATsolvers/clasp_solver -ISATsolvers/clasp_solver/include -ISATsolvers/clasp_solver/program_opts -ISATsolvers/ArgoSATsolver -ISATsolvers/ArgoSATsolver/basic-types -ISATsolvers/ArgoSATsolver/auxiliary -ISATsolvers/ArgoSATsolver/listeners -ISATsolvers/ArgoSATsolver/strategies -ISATsolvers/minisat

ALL_CXXFLAGS = $(CXXFLAGS) -W -Wall $(INCLUDE) $(GMP_FLAGS) $(ADDFLAGS)

sources := $(wildcard Interpreter/*.cpp)
sources += $(wildcard AbstractNumbers/*.cpp)
sources += $(wildcard SMTGenerator/*.cpp)
sources += $(wildcard Utils/*.cpp)
sources += $(wildcard SATsolvers/ArgoSATsolver/*.cpp)
sources += $(wildcard SATsolvers/ArgoSATsolver/auxiliary/*.cpp)
sources += $(wildcard SATsolvers/ArgoSATsolver/basic-types/*.cpp)
sources += $(wildcard SATsolvers/ArgoSATsolver/listeners/*.cpp)
sources += $(wildcard SATsolvers/ArgoSATsolver/strategies/*.cpp)
sources += $(wildcard SATsolvers/clasp_solver/*.cpp)
sources += $(wildcard SATsolvers/clasp_solver/clasp/lib/*.cpp)
sources += $(wildcard SATsolvers/clasp_solver/program_opts/*.cpp)
sources += $(wildcard SATsolvers/minisat/*.cpp)
sources += $(wildcard SATsolvers/minisat/core/*.cc)
sources += $(wildcard SATsolvers/minisat/mtl/*.cc)
sources += $(wildcard SATsolvers/minisat/simp/*.cc)
sources += $(wildcard SATsolvers/minisat/utils/*.cc)

TARGET:=ursa
objects:=$(patsubst %.c,%.o, $(patsubst %.cpp,%.o,$(sources)))
deps:=$(patsubst %.cpp,%.dep,$(sources))
bindir = .

$(TARGET):	$(objects) $(deps) Parser/lex.yy.o Parser/ursa.tab.o 
	$(CXX) $(ALL_CXXFLAGS) $(LDFLAGS) -o $(TARGET) $(objects) Parser/lex.yy.o Parser/ursa.tab.o

Parser/lex.yy.o : Parser/lex.yy.c Parser/ursa.tab.hpp  
	g++ -c -IParser Parser/lex.yy.c
	mv lex.yy.o Parser
Parser/lex.yy.c : Parser/ursa.l
	flex Parser/ursa.l 
	mv lex.yy.c Parser
Parser/ursa.tab.o : Parser/ursa.tab.cpp 
	g++ -c -IParser Parser/ursa.tab.cpp
	mv ursa.tab.o Parser
Parser/ursa.tab.cpp Parser/ursa.tab.hpp: Parser/ursa.ypp
	bison -d Parser/ursa.ypp
	mv ursa.tab.* Parser 

%.o:	%.cpp
	$(CXX) $(ALL_CXXFLAGS) -c $< -o $@

# The SMT solver driver includes the Z3 and cvc5 C++ headers; cvc5 requires
# C++17, so this single unit is built with C++17 and the solver include paths.
Interpreter/SMTSolverDriver.o: Interpreter/SMTSolverDriver.cpp
	$(CXX) $(filter-out -std=c++11,$(ALL_CXXFLAGS)) -std=c++17 $(SMT_DEFINES) $(SMT_INCLUDE) -c $< -o $@

Interpreter/SMTSolverDriver.dep: Interpreter/SMTSolverDriver.cpp
	$(CXX) $(filter-out -std=c++11,$(ALL_CXXFLAGS)) -std=c++17 $(SMT_DEFINES) $(SMT_INCLUDE) -MM $< > $@

%.dep: %.cpp
	$(CXX) $(ALL_CXXFLAGS) $(INCLUDE) -MM $< > $@

.PHONY = clean
clean:
	rm `find . -name '*.dep'` `find . -name '*.o'` `find . -name '*.cpp~'` `find . -name '*.hpp~'` `find . -name '*.cc~'` `find . -name '*.h~'` `find . -name '*.yy.c'` `find . -name '*.tab.cpp'` `find . -name '*.l~'` `find . -name '*.ypp~'` `find . -name '.directory'` 





