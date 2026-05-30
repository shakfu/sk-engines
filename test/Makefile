# Host-only test harness for the pure-logic DSP/timing classes.
# This does NOT build firmware and does NOT use the ARM toolchain; it compiles
# the relevant source files natively so their logic can be exercised off-target.
# Run with: make -C test test

CXX      ?= c++
# -Istubs provides minimal host shims (e.g. daisysp.h) for headers that pull in
# library deps but whose logic under test is pure.
CXXFLAGS := -std=c++17 -O0 -g -Wall -Wextra \
            -Istubs -I../src -I../src/core -I../src/memory -I../src/ui

# Production sources under test (compiled natively, unchanged).
UNIT_SRC := ../src/core/divider.cpp ../src/core/synclock.cpp

# Test sources.
TEST_SRC := test_main.cpp test_divider.cpp test_synclock.cpp test_config.cpp \
            test_wav.cpp test_speedmap.cpp

runner: $(UNIT_SRC) $(TEST_SRC) check.h
	$(CXX) $(CXXFLAGS) $(UNIT_SRC) $(TEST_SRC) -o runner

test: runner
	./runner

clean:
	rm -f runner

.PHONY: test clean
