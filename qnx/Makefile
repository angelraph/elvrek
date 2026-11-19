# Elvrek: QNX 8.0 target build.
# Source the SDP environment first:  source ~/qnx800/qnxsdp-env.sh
# Then:  make -C qnx            (x86_64, for QEMU)
#        make -C qnx ARCH=aarch64le   (Raspberry Pi 5)
ARCH    ?= x86_64
ifeq ($(ARCH),x86_64)
  V = gcc_ntox86_64
else
  V = gcc_ntoaarch64le
endif
QCC     = qcc -V$(V)
ROOT    = ..
OUT     = $(ROOT)/build/qnx-$(ARCH)
CFLAGS  = -std=gnu11 -O2 -Wall -Wextra -Wno-missing-field-initializers \
          -I$(ROOT)/include -I$(ROOT)/src/core -I$(ROOT)/src/plant \
          -I$(ROOT)/src/ecg -I$(ROOT)/src/advisor -I$(ROOT)/src/qnx
CORE    = $(ROOT)/src/core/elv_core.c $(ROOT)/src/core/elv_inv.c
PLANT   = $(ROOT)/src/plant/elv_plant.c $(ROOT)/src/ecg/elv_ecg.c
ADV     = $(ROOT)/src/advisor/elv_advisor.c

BINS = elvrekd elv-plant elv-advisor elv-ctl elv-fault elv-telemetry elv-super
all: $(addprefix $(OUT)/,$(BINS))

$(OUT):
	mkdir -p $@

$(OUT)/elvrekd: $(ROOT)/src/qnx/elvrekd.c $(CORE) | $(OUT)
	$(QCC) $(CFLAGS) -o $@ $^ -lm -lslog2
$(OUT)/elv-plant: $(ROOT)/src/qnx/elv_plant_main.c $(PLANT) | $(OUT)
	$(QCC) $(CFLAGS) -o $@ $^ -lm
$(OUT)/elv-advisor: $(ROOT)/src/qnx/elv_advisor_main.c $(ADV) | $(OUT)
	$(QCC) $(CFLAGS) -o $@ $^ -lm
$(OUT)/elv-ctl: $(ROOT)/src/qnx/elv_ctl.c | $(OUT)
	$(QCC) $(CFLAGS) -o $@ $^
$(OUT)/elv-fault: $(ROOT)/src/qnx/elv_fault.c | $(OUT)
	$(QCC) $(CFLAGS) -o $@ $^
$(OUT)/elv-telemetry: $(ROOT)/src/qnx/elv_telemetry.c | $(OUT)
	$(QCC) $(CFLAGS) -o $@ $^ -lsocket
$(OUT)/elv-super: $(ROOT)/src/qnx/elv_super.c | $(OUT)
	$(QCC) $(CFLAGS) -o $@ $^

clean:
	rm -rf $(OUT)
.PHONY: all clean
