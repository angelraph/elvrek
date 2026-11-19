# Elvrek: host build (Linux/WSL gcc). The QNX build lives in qnx/Makefile.
CC      ?= gcc
CFLAGS  ?= -std=c11 -O2 -Wall -Wextra -Werror -Wno-missing-field-initializers -D_POSIX_C_SOURCE=200809L
INC      = -Iinclude -Isrc/core -Isrc/plant -Isrc/ecg -Isrc/advisor

CORE     = src/core/elv_core.c src/core/elv_inv.c
SIM      = src/plant/elv_plant.c src/ecg/elv_ecg.c src/advisor/elv_advisor.c

build/scenarios: tests/host/scenarios.c $(CORE) $(SIM) $(wildcard include/*.h src/*/*.h)
	@mkdir -p build
	$(CC) $(CFLAGS) $(INC) -o $@ tests/host/scenarios.c $(CORE) $(SIM) -lm

.PHONY: test clean
test: build/scenarios
	./build/scenarios

clean:
	rm -rf build

# Portable target processes, built on the host for smoke tests. elvrekd
# itself needs the QNX headers (resmgr, pulses, procmgr): build it with qnx/Makefile.
HOSTP   = build/host
PCFLAGS = -std=gnu11 -O2 -Wall -Wextra -Werror -Wno-missing-field-initializers -D_GNU_SOURCE $(INC) -Isrc/qnx
host-procs: $(HOSTP)/elv-plant $(HOSTP)/elv-advisor $(HOSTP)/elv-ctl $(HOSTP)/elv-fault $(HOSTP)/elv-telemetry $(HOSTP)/elv-super
$(HOSTP):
	mkdir -p $@
$(HOSTP)/elv-plant: src/qnx/elv_plant_main.c src/plant/elv_plant.c src/ecg/elv_ecg.c | $(HOSTP)
	$(CC) $(PCFLAGS) -o $@ $^ -lm -lpthread -lrt
$(HOSTP)/elv-advisor: src/qnx/elv_advisor_main.c src/advisor/elv_advisor.c | $(HOSTP)
	$(CC) $(PCFLAGS) -o $@ $^ -lm -lpthread -lrt
$(HOSTP)/elv-%: src/qnx/elv_%.c | $(HOSTP)
	$(CC) $(PCFLAGS) -o $@ $< -lpthread -lrt
.PHONY: host-procs

# Rhythm evaluation on real PhysioNet arrest ECG (run tools/fetch_physionet.py first).
build/ecg_eval: tests/host/ecg_eval.c $(CORE) src/advisor/elv_advisor.c
	@mkdir -p build
	$(CC) $(CFLAGS) $(INC) -o $@ $^ -lm
.PHONY: ecg-eval
ecg-eval: build/ecg_eval
	./build/ecg_eval build/ecg/*.bin

# Recorded run of the real controller for the website's playback.
build/trace_export: tests/host/trace_export.c $(CORE) $(SIM)
	@mkdir -p build
	$(CC) $(CFLAGS) $(INC) -o $@ $^ -lm
.PHONY: web-trace
web-trace: build/trace_export
	./build/trace_export web/data/run-vf-shock.json

# Tune the trusted rhythm check on CUDB and score it once on VFDB.
build/rhythm_tune: tests/host/rhythm_tune.c $(CORE)
	@mkdir -p build
	$(CC) $(CFLAGS) $(INC) -o $@ $^ -lm
.PHONY: rhythm-tune
rhythm-tune: build/rhythm_tune
	./build/rhythm_tune build/ecg/*.bin
