# daima-agent 内核风格 Makefile
VERSION = 1
PATCHLEVEL = 0

export TOPDIR := $(CURDIR)
export DAIMA_DIR := $(TOPDIR)/daima

ARCH ?= host
export ARCH
include $(DAIMA_DIR)/arch/$(ARCH)/Makefile

.DEFAULT_GOAL := all

menuconfig:
	python3 scripts/menuconfig.py
defconfig:
	python3 kconfig.py defconfig
	make oldconfig
config:
	python3 kconfig.py list

all: host
host:
	@echo "  BUILD   daima ($(ARCH))"
	cmake -B build-host -DCMAKE_C_FLAGS="$(DAIMA_CFLAGS)" -DCMAKE_BUILD_TYPE=Release
	cmake --build build-host
mips:
	$(MAKE) ARCH=mips host
arm:
	$(MAKE) ARCH=arm host

modules:
	@echo "  MODULES  extensions/ (built-in)"

test:
	$(MAKE) -C test test PWD=$(CURDIR)

clean:
	rm -rf build-host build-mips build-arm
	$(MAKE) -C test clean
mrproper: clean
	rm -f .config .config.old
distclean: mrproper
	rm -rf build-*

help:
	@echo "make / make host     build x86_64"
	@echo "make mips|arm        cross-compile"
	@echo "make menuconfig      interactive config"
	@echo "make test clean mrproper distclean"
