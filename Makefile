# daima-agent 内核风格 Makefile
VERSION = 1
PATCHLEVEL = 0

export TOPDIR := $(CURDIR)
export srctree := $(TOPDIR)
export objtree := $(TOPDIR)
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

# 核心目录列表 (Kbuild 风格 obj-y 清单)
core-y := init/
core-y += kernel/ kernel/sched/ kernel/time/ kernel/printk/ kernel/irq/ kernel/driver/
core-y += ipc/ lib/ net/ fs/
core-y += drivers/llm/ drivers/channel/feishu/ drivers/channel/vector/ drivers/channel/gateway/
core-y += drivers/tool/ drivers/memory/ drivers/skill/ drivers/voice/
core-y += drivers/platform/ drivers/pet/
core-y += extensions/

# 递归构建入口；P2 阶段保留 cmake 后端，Kbuild 目标用于验证目录 obj-y。
daima-dirs := $(patsubst %/,%,$(core-y))
daima_builtin := $(foreach d,$(daima-dirs),$(d)/built-in.o)

.PHONY: all host mips arm modules test clean mrproper distclean help menuconfig defconfig config
.PHONY: $(daima-dirs) kbuild kbuild-doc-check
$(daima-dirs):
	$(Q)$(MAKE) -f $(TOPDIR)/scripts/Makefile.build obj=daima/$@

kbuild: $(daima-dirs)
	@:

kbuild-doc-check:
	$(Q)$(MAKE) KBUILD_DOC_ONLY=1 kbuild

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
	@echo "make kbuild          walk Kbuild obj-y directories"
	@echo "make mips|arm        cross-compile"
	@echo "make menuconfig      interactive config"
	@echo "make test clean mrproper distclean"
