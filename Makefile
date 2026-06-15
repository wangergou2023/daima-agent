# agent 内核风格 Makefile
VERSION = 1
PATCHLEVEL = 0

export TOPDIR := $(CURDIR)
export srctree := $(TOPDIR)
export objtree := $(TOPDIR)
export AGENT_ROOT := $(TOPDIR)
export AGENT_DIR := $(AGENT_ROOT)
BUILD_DIR := build-kbuild
export BUILD_DIR
AGENT_BIN := $(BUILD_DIR)/daima

ARCH ?= host
export ARCH
include $(AGENT_DIR)/arch/$(ARCH)/Makefile
include $(TOPDIR)/scripts/Kbuild.include
export CC AGENT_CFLAGS LDFLAGS

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

agent-dirs := $(patsubst %/,%,$(core-y))
agent_builtin := $(foreach d,$(agent-dirs),$(d)/built-in.o)

.PHONY: all daima host mips arm arch-obj cjson modules test clean mrproper distclean help menuconfig defconfig config $(agent-dirs)

all: kbuild
kbuild: daima

$(BUILD_DIR):
	$(Q)mkdir -p $(BUILD_DIR)
	$(Q)> $(BUILD_DIR)/objects.list

$(agent-dirs): $(BUILD_DIR)
	$(Q)$(MAKE) -f $(TOPDIR)/scripts/Makefile.build obj=$@

cjson: $(BUILD_DIR)
	@echo "  CC      $(BUILD_DIR)/cjson.o"
	$(Q)$(CC) $(AGENT_CFLAGS) -c -o $(BUILD_DIR)/cjson.o $(TOPDIR)/third_party/cjson/cJSON.c
	@echo $(BUILD_DIR)/cjson.o >> $(BUILD_DIR)/objects.list

arch-obj: $(BUILD_DIR)
	$(Q)$(MAKE) -f $(TOPDIR)/scripts/Makefile.build obj=arch/$(ARCH)

daima: $(agent-dirs) cjson arch-obj
	@echo "  LD      daima"
	$(Q)awk '!seen[$$0]++' $(BUILD_DIR)/objects.list > $(BUILD_DIR)/objects.link
	$(Q)$(CC) $(AGENT_CFLAGS) -o $(AGENT_BIN) @$(BUILD_DIR)/objects.link $(LDFLAGS)
	@echo "  DONE    daima"

kbuild-doc-check:
	$(Q)$(MAKE) KBUILD_DOC_ONLY=1 kbuild

menuconfig:
	python3 scripts/menuconfig.py
defconfig:
	python3 scripts/kconfig.py defconfig
	make oldconfig
config:
	python3 scripts/kconfig.py list

host: kbuild
mips:
	$(MAKE) ARCH=mips kbuild
arm:
	$(MAKE) ARCH=arm kbuild


modules:
	@echo "  MODULES  extensions/ (built-in)"

test:
	$(Q)mkdir -p test/build
	$(MAKE) -C test test PWD=$(CURDIR)

clean:
	$(Q)rm -rf build-kbuild build-host build-mips build-arm
	$(Q)find init kernel drivers arch ipc lib net fs include extensions -name '*.o' -delete
	$(Q)$(MAKE) -C test clean

kbuild-clean:
	$(Q)rm -rf $(BUILD_DIR)
	$(Q)find init kernel drivers arch ipc lib net fs include extensions -name '*.o' -delete

mrproper: clean
	$(Q)rm -f .config .config.old
distclean: mrproper
	rm -rf build-*

help:
	@echo "make / make host     build x86_64 via Kbuild"
	@echo "make mips|arm        cross-compile"
	@echo "make menuconfig      interactive config"
	@echo "make test clean mrproper distclean"
