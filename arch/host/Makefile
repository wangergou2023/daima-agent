# arch/host/Makefile
CROSS_COMPILE ?=
CC := $(CROSS_COMPILE)gcc

UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
    BREW_PREFIX := $(shell brew --prefix 2>/dev/null)
    ifneq ($(BREW_PREFIX),)
        BREW_OPENSSL := $(shell brew --prefix openssl@3 2>/dev/null || brew --prefix openssl 2>/dev/null)
        BREW_CURL := $(shell brew --prefix curl 2>/dev/null)
    endif
endif

AGENT_CFLAGS := -I$(AGENT_DIR) -I$(AGENT_DIR)/include -I$(AGENT_DIR)/include/generated \
    -iquote $(AGENT_DIR)/init -iquote $(AGENT_DIR)/kernel \
    -iquote $(AGENT_DIR)/kernel/time -iquote $(AGENT_DIR)/ipc \
    -iquote $(AGENT_DIR)/lib -iquote $(AGENT_DIR)/net -iquote $(AGENT_DIR)/fs \
    -iquote $(AGENT_DIR)/include/linux \
    -iquote $(AGENT_DIR)/kernel/printk \
    -std=gnu11 -Wall -Wextra -D_GNU_SOURCE -DAGENT_HOST=1 -DENABLE_VISION=1

ifneq ($(BREW_OPENSSL),)
    AGENT_CFLAGS += -I$(BREW_OPENSSL)/include
    LDFLAGS := -L$(BREW_OPENSSL)/lib -lcurl -lssl -lcrypto -lm -lpthread
else
    LDFLAGS := -lcurl -lssl -lcrypto -ldl -lm -lpthread
endif
ifneq ($(BREW_CURL),)
    AGENT_CFLAGS += -I$(BREW_CURL)/include
    LDFLAGS += -L$(BREW_CURL)/lib
endif

obj-y := portability.o audio_io_stub.o vision_capture_stub.o voice_wake_stub.o
obj-y += ws_server_host.o llm_http_client_host.o llm_proxy_host.o
