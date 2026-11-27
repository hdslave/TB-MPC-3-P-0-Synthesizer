# Slink: a CLAP bass synthesizer with the TB-3PO sequencer for the Akai MPC Sample (64-bit ARM Linux, Cortex-A55).
# Build inside a Linux arm64 image with ./build.sh, which runs `make all test`.
#   make            build/Slink.clap
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
all: $(OUT)/Slink.clap

$(OUT)/Slink.clap: $(SOURCES) $(HEADERS) src/exports.map Makefile
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

test: $(OUT)/Slink.clap $(OUT)/host $(OUT)/reference $(OUT)/fingerprint
	$(OUT)/reference
	$(OUT)/host $(OUT)/Slink.clap $(OUT)
	$(OUT)/fingerprint $(OUT)/Slink.clap tests/fingerprint.txt > /dev/null

# The sound's hashes after a change meant to change the sound (see tests/fingerprint.cpp).
fingerprint-update: $(OUT)/Slink.clap $(OUT)/fingerprint
	$(OUT)/fingerprint $(OUT)/Slink.clap > tests/fingerprint.txt

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
	$(CXX) $(subst -O3,-O1,$(CXXFLAGS)) $(SAN) $(SOURCES) -shared -Wl,--version-script=src/exports.map -o $(OUT)/asan/Slink.clap
	$(CXX) -std=c++17 -O1 $(WARN) $(SAN) -Ithird_party/clap/include -Isrc tests/host.cpp -ldl -o $(OUT)/asan/host
	$(CXX) -std=c++17 -O1 $(WARN) $(SAN) -Isrc tests/reference.cpp -o $(OUT)/asan/reference
	$(CXX) -std=c++17 -O1 $(WARN) $(SAN) -Ithird_party/clap/include tests/fingerprint.cpp -ldl -o $(OUT)/asan/fingerprint
	$(SAN_ENV) $(OUT)/asan/reference
	$(SAN_ENV) $(OUT)/asan/host $(OUT)/asan/Slink.clap $(OUT)/asan
	@$(SAN_ENV) $(OUT)/asan/fingerprint $(OUT)/asan/Slink.clap tests/fingerprint.txt > $(OUT)/asan/fingerprint.log 2>&1; \
	    rc=$$?; if [ $$rc -gt 1 ]; then cat $(OUT)/asan/fingerprint.log; exit $$rc; fi; echo "sanitize: every fingerprint scenario ran clean"

# src/scales.h from the firmware's files in tools/oc-source (run on any machine with Python 3).
scales:
	python3 tools/gen_scales.py

clean:
	rm -rf $(OUT)

# freesp16's ARM EABI/glibc 2.13 toolchain, and a Linux host copy for screen tests.
SP16_ART := src/sp16_thorn_bank.S
SP16_MEDIA := assets/sp16-thorn/panel.bank assets/sp16-thorn/index.h
SP16_CXX ?= arm-linux-gnueabi-g++
SP16_OPT := -O3 -march=armv7-a -mtune=cortex-a9 -mfpu=neon -mfloat-abi=softfp -ffp-contract=fast -fno-math-errno -fno-trapping-math
SP16_VERSION := 0.9.10
SP16_FLAGS := -DTBMPC_SP16 -DTBMPC_VERSION='"$(SP16_VERSION)"' -ffunction-sections -fdata-sections
.PHONY: sp16 sp16-preview
sp16: $(OUT)/sp16/Slink.clap
sp16-preview: $(OUT)/sp16-preview/Slink.clap

$(OUT)/sp16/Slink.clap: $(SP16_ART) $(SP16_MEDIA) $(SOURCES) $(HEADERS) src/exports.map Makefile
	@mkdir -p $(@D)
	$(SP16_CXX) $(subst $(OPT),$(SP16_OPT),$(CXXFLAGS)) $(SP16_FLAGS) $(SOURCES) $(SP16_ART) $(LDFLAGS) -o $@

$(OUT)/sp16-preview/Slink.clap: $(SP16_ART) $(SP16_MEDIA) $(SOURCES) $(HEADERS) src/exports.map Makefile
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(SP16_FLAGS) $(SOURCES) $(SP16_ART) $(LDFLAGS) -o $@

$(OUT)/sp16/host: tests/host.cpp tests/png.h tests/wav.h $(HEADERS) Makefile
	@mkdir -p $(@D)
	$(SP16_CXX) -std=c++17 -O2 $(WARN) -march=armv7-a -mfpu=neon -mfloat-abi=softfp -Ithird_party/clap/include -Isrc $< -static-libgcc -static-libstdc++ -ldl -o $@

.PHONY: sp16-test
sp16-test: sp16 $(OUT)/sp16/host
	sp16-check-glibc $(OUT)/sp16/Slink.clap
	qemu-arm -cpu cortex-a9 -L /opt/sp16/sysroot $(OUT)/sp16/host $(OUT)/sp16/Slink.clap $(OUT)/sp16 --sp16
