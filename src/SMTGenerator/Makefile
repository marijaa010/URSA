CXX = c++
CXXFLAGS = -std=c++11 -Wall -Wextra -O2

COMMON_SRCS = SMTExpr.cpp SMTNumber.cpp SMTBoolean.cpp
COMMON_OBJS = $(COMMON_SRCS:.cpp=.o)

all: smt_demo

smt_demo: $(COMMON_OBJS) main.o
	$(CXX) $(CXXFLAGS) -o smt_demo $(COMMON_OBJS) main.o

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

test: smt_demo
	@./smt_demo

clean:
	rm -f *.o smt_demo

.PHONY: all test clean
