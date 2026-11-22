# Elvrek: host build (Linux/WSL gcc). The QNX build lives in qnx/Makefile.
CC      ?= gcc
CFLAGS  ?= -std=c11 -O2 -Wall -Wextra -Werror -Wno-missing-field-initializers -D_POSIX_C_SOURCE=200809L
INC      = -Iinclude -Isrc/core -Isrc/plant -Isrc/ecg -Isrc/advisor

CORE     = src/core/elv_core.c src/core/elv_inv.c src/core/elv_cancel.c src/core/elv_rhythm.c
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

# Rhythm read through compressions: features for every window, then train
# on CUDB and score VFDB (needs the PhysioNet files, see tools/fetch_physionet.py).
build/zp_dump: tests/host/zp_dump.c $(CORE)
	@mkdir -p build
	$(CC) $(CFLAGS) $(INC) -o $@ $^ -lm
.PHONY: zero-pause
zero-pause: build/zp_dump
	@mkdir -p build/zp
	./build/zp_dump build/zp/features.csv build/ecg/*.bin
	python3 -I tools/zp_train.py build/zp/features.csv include/elv_rhythm_model.h

# Crew console: draw six controller states on the host, and replay a console
# log recorded on the QNX target through the same drawing code.
CONSOLE  = src/console/elv_console_draw.c
build/console_preview: tests/host/console_preview.c $(CONSOLE) include/elv_font.h src/console/elv_console_draw.h
	@mkdir -p build
	$(CC) $(CFLAGS) $(INC) -Isrc/console -o $@ tests/host/console_preview.c $(CONSOLE) -lm
build/console_replay: tests/host/console_replay.c $(CONSOLE) include/elv_font.h src/console/elv_console_draw.h
	@mkdir -p build
	$(CC) $(CFLAGS) $(INC) -Isrc/console -o $@ tests/host/console_replay.c $(CONSOLE) -lm
.PHONY: console
console: build/console_preview build/console_replay
	@mkdir -p build/console
	./build/console_preview build/console
