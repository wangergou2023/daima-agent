# daima/arch/arm/Makefile
CROSS_COMPILE ?= arm-linux-gnueabihf-
CC := $(CROSS_COMPILE)gcc
DAIMA_CFLAGS := -I$(DAIMA_DIR) -I$(DAIMA_DIR)/include -I$(DAIMA_DIR)/include/generated \
    -iquote $(DAIMA_DIR)/init -iquote $(DAIMA_DIR)/kernel \
    -iquote $(DAIMA_DIR)/kernel/time -iquote $(DAIMA_DIR)/ipc \
    -iquote $(DAIMA_DIR)/lib -iquote $(DAIMA_DIR)/net -iquote $(DAIMA_DIR)/fs \
    -iquote $(DAIMA_DIR)/include/linux \
    -iquote $(DAIMA_DIR)/kernel/printk -iquote $(DAIMA_DIR)/kernel/irq \
    -std=gnu11 -Wall -Wextra -D_GNU_SOURCE -DDAIMA_HOST=1 -DENABLE_VISION=1
LDFLAGS := -lcurl -lssl -lcrypto -ldl -lm -lpthread

obj-y := audio_io_arm.o vision_capture_arm.o voice_wake_arm.o
obj-y += ../host/ws_server_host.o ../host/llm_http_client_host.o ../host/llm_proxy_host.o
