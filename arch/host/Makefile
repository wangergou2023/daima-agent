# arch/host/Makefile
CROSS_COMPILE ?=
CC := $(CROSS_COMPILE)gcc
AGENT_CFLAGS := -I$(AGENT_DIR) -I$(AGENT_DIR)/include -I$(AGENT_DIR)/include/generated \
    -iquote $(AGENT_DIR)/init -iquote $(AGENT_DIR)/kernel \
    -iquote $(AGENT_DIR)/kernel/time -iquote $(AGENT_DIR)/ipc \
    -iquote $(AGENT_DIR)/lib -iquote $(AGENT_DIR)/net -iquote $(AGENT_DIR)/fs \
    -iquote $(AGENT_DIR)/include/linux \
    -iquote $(AGENT_DIR)/kernel/printk -iquote $(AGENT_DIR)/kernel/irq \
    -std=gnu11 -Wall -Wextra -D_GNU_SOURCE -DAGENT_HOST=1 -DENABLE_VISION=1
LDFLAGS := -lcurl -lssl -lcrypto -ldl -lm -lpthread

obj-y := audio_io_stub.o vision_capture_stub.o voice_wake_stub.o
obj-y += ws_server_host.o llm_http_client_host.o llm_proxy_host.o
