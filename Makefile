# =====================================================================
# XR_Playground - VITURE XR Glasses sample code.
#
# Each directory under examples/ is a self-contained sample with its own
# Makefile. This top-level Makefile just fans out across them.
#
#   make            build every example
#   make list       show the available examples
#   make clean      clean every example
#
# To work on one sample:  cd examples/01_camera_feed && make run
# =====================================================================

EXAMPLES := $(sort $(dir $(wildcard examples/*/Makefile)))

.PHONY: all clean list $(EXAMPLES)

all: $(EXAMPLES)

$(EXAMPLES):
	@$(MAKE) --no-print-directory -C $@

list:
	@echo "Available examples:"
	@for e in $(EXAMPLES); do echo "  $$e"; done

clean:
	@for e in $(EXAMPLES); do $(MAKE) --no-print-directory -C $$e clean; done
