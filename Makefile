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
AGENT_BIN := $(BUILD_DIR)/agent
AGENT_CFLAGS += -iquote $(TOPDIR)/kernel/turn
AGENT_CFLAGS += -iquote $(TOPDIR)/kernel/channel
AGENT_CFLAGS += -iquote $(TOPDIR)/kernel/runtime
AGENT_CFLAGS += -iquote $(TOPDIR)/kernel/context
AGENT_CFLAGS += -iquote $(TOPDIR)/kernel/tooling
AGENT_CFLAGS += -iquote $(TOPDIR)/kernel/tooling/delegate

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
core-y += kernel/ kernel/turn/ kernel/time/ kernel/printk/
core-y += kernel/channel/ kernel/runtime/ kernel/context/ kernel/tooling/
core-y += kernel/tooling/delegate/
core-y += ipc/ lib/ net/ fs/
core-y += drivers/llm/ drivers/channel/feishu/ drivers/channel/vector/ drivers/channel/gateway/
core-y += drivers/tool/ drivers/memory/ drivers/skill/ drivers/voice/
core-y += drivers/platform/ drivers/pet/

agent-dirs := $(patsubst %/,%,$(core-y))
agent_builtin := $(foreach d,$(agent-dirs),$(d)/built-in.o)

.PHONY: all agent host mips arm arch-obj cjson modules clean mrproper distclean help $(agent-dirs)

all: kbuild
kbuild: agent

$(BUILD_DIR):
	$(Q)mkdir -p $(BUILD_DIR)
	$(Q)> $(BUILD_DIR)/objects.list

$(agent-dirs): $(BUILD_DIR)
	$(Q)$(MAKE) -f $(TOPDIR)/scripts/Makefile.build obj=$@

cjson: $(BUILD_DIR)
	@echo "  CC      $(BUILD_DIR)/cjson.o"
	$(Q)$(CC) $(AGENT_CFLAGS) -c -o $(BUILD_DIR)/cjson.o $(TOPDIR)/lib/cjson.c
	@echo $(BUILD_DIR)/cjson.o >> $(BUILD_DIR)/objects.list

arch-obj: $(BUILD_DIR)
	$(Q)$(MAKE) -f $(TOPDIR)/scripts/Makefile.build obj=arch/$(ARCH)

agent: $(agent-dirs) cjson arch-obj
	@echo "  LD      agent"
	$(Q)awk '!seen[$$0]++' $(BUILD_DIR)/objects.list > $(BUILD_DIR)/objects.link
	$(Q)$(CC) $(AGENT_CFLAGS) -o $(AGENT_BIN) @$(BUILD_DIR)/objects.link $(LDFLAGS)
	@echo "  DONE    agent"

kbuild-doc-check:
	$(Q)$(MAKE) KBUILD_DOC_ONLY=1 kbuild


host: kbuild
mips:
	$(MAKE) ARCH=mips kbuild
arm:
	$(MAKE) ARCH=arm kbuild


modules:
	@echo "  MODULES  none"

clean:
	$(Q)rm -rf build-kbuild build-host build-mips build-arm
	$(Q)find init kernel drivers arch ipc lib net fs include -name '*.o' -delete
	$(Q)find init kernel drivers arch ipc lib net fs include -name '*.d' -delete

kbuild-clean:
	$(Q)rm -rf $(BUILD_DIR)
	$(Q)find init kernel drivers arch ipc lib net fs include -name '*.o' -delete
	$(Q)find init kernel drivers arch ipc lib net fs include -name '*.d' -delete

mrproper: clean
	$(Q)rm -f .config .config.old
distclean: mrproper
	rm -rf build-*

help:
	@echo "make / make host     build x86_64 via Kbuild"
	@echo "make mips|arm        cross-compile"
	@echo "make clean mrproper distclean"
