# TB-MPC-3-P-0: a CLAP bass synthesizer with the TB-3PO sequencer for the Akai MPC Sample (64-bit ARM Linux, Cortex-A55).
# Build inside a Linux arm64 image with ./build.sh, which runs `make all test`.
#   make            build/TB-MPC-3-P-0.clap
#   make test       the tests against the built plug-in (tests/host.cpp), the port against the applet (tests/reference.cpp)
#                   and the sound's fingerprint (tests/fingerprint.cpp: the output to the bit, against tests/fingerprint.txt)
#   make bench      build/bench: renders a busy pattern; tools/bench.sh counts its instructions
#   make sanitize   the tests and the fingerprint's scenarios under AddressSanitizer and UBSan (build/asan)
CXX ?= g++
OUT := build
WARN := -Wall -Wextra -Werror
# The MPC's cores. -ffp-contract=fast lets the compiler fuse multiply-adds; nothing here relies on IEEE errno,
# traps or exceptions.
OPT := -O3 -mcpu=cortex-a55 -ffp-contract=fast -fno-math-errno -fno-trapping-math
CXXFLAGS := -std=c++17 $(OPT) $(WARN) -fno-exceptions -fno-rtti -fvisibility=hidden -fPIC -Ithird_party/clap/include -Isrc
# Only clap_entry is exported; libstdc++ and libgcc are linked in so the MPC needs nothing beyond libc and libm. -s
# leaves out the symbol table (the code is the same; the exported clap_entry stays).
LDFLAGS := -shared -s -static-libgcc -static-libstdc++ -Wl,--gc-sections -Wl,-z,now -Wl,-O1 -Wl,--version-script=src/exports.map

SOURCES := src/tbmpc.cpp
HEADERS := $(wildcard src/*.h src/ui/*.h)

.PHONY: all test bench clean scales fingerprint-update sanitize
all: $(OUT)/TB-MPC-3-P-0.clap

$(OUT)/TB-MPC-3-P-0.clap: $(SOURCES) $(HEADERS) src/exports.map Makefile
	@mkdir -p $(OUT)
	$(CXX) $(CXXFLAGS) $(SOURCES) $(LDFLAGS) -o $@

$(OUT)/host: tests/host.cpp tests/wav.h tests/png.h $(HEADERS) Makefile
	@mkdir -p $(OUT)
	$(CXX) -std=c++17 -O2 $(WARN) -Ithird_party/clap/include -Isrc tests/host.cpp -ldl -o $@

$(OUT)/reference: tests/reference.cpp tests/applet.h $(HEADERS) Makefile
	@mkdir -p $(OUT)
	$(CXX) -std=c++17 -O2 $(WARN) -Isrc tests/reference.cpp -o $@

$(OUT)/fingerprint: tests/fingerprint.cpp tests/fingerprint_scenarios.h Makefile
	@mkdir -p $(OUT)
	$(CXX) -std=c++17 -O2 $(WARN) -Ithird_party/clap/include tests/fingerprint.cpp -ldl -o $@

test: $(OUT)/TB-MPC-3-P-0.clap $(OUT)/host $(OUT)/reference $(OUT)/fingerprint
	$(OUT)/reference
	$(OUT)/host $(OUT)/TB-MPC-3-P-0.clap $(OUT)
	$(OUT)/fingerprint $(OUT)/TB-MPC-3-P-0.clap tests/fingerprint.txt > /dev/null

# The sound's hashes after a change meant to change the sound (see tests/fingerprint.cpp).
fingerprint-update: $(OUT)/TB-MPC-3-P-0.clap $(OUT)/fingerprint
	$(OUT)/fingerprint $(OUT)/TB-MPC-3-P-0.clap > tests/fingerprint.txt

$(OUT)/bench: tests/bench.cpp $(SOURCES) $(HEADERS) Makefile
	@mkdir -p $(OUT)
	$(CXX) -std=c++17 $(OPT) $(WARN) -g -Ithird_party/clap/include -Isrc tests/bench.cpp -o $@

bench: $(OUT)/bench

# The tests under AddressSanitizer and UBSan (build/asan): a read past a buffer, a leak or undefined behaviour stops
# them (abort_on_error, so a report is never taken for a plain exit 1). The fingerprint's hashes hold only for the
# release flags, so their mismatch (exit 1) is expected here; every scenario must still run clean.
SAN := -fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer -g1
SAN_ENV := ASAN_OPTIONS=detect_leaks=1:abort_on_error=1 UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1:abort_on_error=1
sanitize:
	@mkdir -p $(OUT)/asan
	$(CXX) $(subst -O3,-O1,$(CXXFLAGS)) $(SAN) $(SOURCES) -shared -Wl,--version-script=src/exports.map -o $(OUT)/asan/TB-MPC-3-P-0.clap
	$(CXX) -std=c++17 -O1 $(WARN) $(SAN) -Ithird_party/clap/include -Isrc tests/host.cpp -ldl -o $(OUT)/asan/host
	$(CXX) -std=c++17 -O1 $(WARN) $(SAN) -Isrc tests/reference.cpp -o $(OUT)/asan/reference
	$(CXX) -std=c++17 -O1 $(WARN) $(SAN) -Ithird_party/clap/include tests/fingerprint.cpp -ldl -o $(OUT)/asan/fingerprint
	$(SAN_ENV) $(OUT)/asan/reference
	$(SAN_ENV) $(OUT)/asan/host $(OUT)/asan/TB-MPC-3-P-0.clap $(OUT)/asan
	@$(SAN_ENV) $(OUT)/asan/fingerprint $(OUT)/asan/TB-MPC-3-P-0.clap tests/fingerprint.txt > $(OUT)/asan/fingerprint.log 2>&1; \
	    rc=$$?; if [ $$rc -gt 1 ]; then cat $(OUT)/asan/fingerprint.log; exit $$rc; fi; echo "sanitize: every fingerprint scenario ran clean"

# src/scales.h from the firmware's files in tools/oc-source (run on any machine with Python 3).
scales:
	python3 tools/gen_scales.py

clean:
	rm -rf $(OUT)
