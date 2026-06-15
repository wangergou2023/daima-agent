# daima/arch/mips/Makefile
CROSS_COMPILE ?= mips-linux-uclibc-gnu-
CC := $(CROSS_COMPILE)gcc
DAIMA_CFLAGS := -I$(DAIMA_DIR) -I$(DAIMA_DIR)/include -I$(DAIMA_DIR)/include/generated \
    -iquote $(DAIMA_DIR)/init -iquote $(DAIMA_DIR)/kernel \
    -iquote $(DAIMA_DIR)/kernel/time -iquote $(DAIMA_DIR)/ipc \
    -iquote $(DAIMA_DIR)/lib -iquote $(DAIMA_DIR)/net -iquote $(DAIMA_DIR)/fs \
    -iquote $(DAIMA_DIR)/include/linux \
    -iquote $(DAIMA_DIR)/kernel/printk -iquote $(DAIMA_DIR)/kernel/irq \
    -std=gnu11 -Wall -Wextra -D_GNU_SOURCE -DDAIMA_HOST=1 -DENABLE_VISION=1
LDFLAGS := -lcurl -lssl -lcrypto -ldl -lm -lpthread -latomic -limp -lsysutils -lalog -lrt -lstdc++

obj-y := audio_io_mips.o vision_capture_mips.o voice_wake_mips.o
obj-y += ../host/ws_server_host.o ../host/llm_http_client_host.o ../host/llm_proxy_host.o
