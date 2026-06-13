# daima-agent 内核风格 Makefile
VERSION = 1
PATCHLEVEL = 0

export TOPDIR := $(CURDIR)
export DAIMA_DIR := $(TOPDIR)/daima

ARCH ?= host
export ARCH
include $(DAIMA_DIR)/arch/$(ARCH)/Makefile
include $(TOPDIR)/scripts/Kbuild.include

# 输出目录 (内核风格: make O=build)
O ?= build-host
ifeq ($(O),.)
  O := build-host
endif

# 详细输出
ifeq ($(V),1)
  Q :=
else
  Q := @
endif

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
	$(Q)echo "  BUILD   daima ($(ARCH)) → $(O)/"
	$(Q)cmake -B $(O) -DCMAKE_C_FLAGS="$(DAIMA_CFLAGS)" \
		-DCMAKE_BUILD_TYPE=Release
	$(Q)cmake --build $(O)
mips:
	$(MAKE) ARCH=mips O=build-mips host
arm:
	$(MAKE) ARCH=arm O=build-arm host

modules:
	@echo "  MODULES  extensions/ (built-in)"

test:
	$(MAKE) -C test test PWD=$(CURDIR)

clean:
	$(Q)rm -rf build-host build-mips build-arm
	$(Q)$(MAKE) -C test clean

mrproper: clean
	$(Q)rm -f .config .config.old
distclean: mrproper
	rm -rf build-*

help:
	@echo "make / make host     build x86_64"
	@echo "make mips|arm        cross-compile"
	@echo "make menuconfig      interactive config"
	@echo "make test clean mrproper distclean"
