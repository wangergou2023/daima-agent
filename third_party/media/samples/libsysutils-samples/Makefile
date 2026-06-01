CONFIG_UCLIBC_BUILD=y

CROSS_COMPILE ?= mips-linux-uclibc-gnu-

CC = $(CROSS_COMPILE)gcc
CPLUSPLUS = $(CROSS_COMPILE)g++
LD = $(CROSS_COMPILE)ld
AR = $(CROSS_COMPILE)ar cr
STRIP = $(CROSS_COMPILE)strip

CFLAGS = $(INCLUDES) -O2 -Wall -march=mips32r2

ifeq ($(CONFIG_UCLIBC_BUILD), y)
	CFLAGS += -muclibc
	LDFLAG += -muclibc
endif

ifeq ($(CONFIG_UCLIBC_BUILD), y)
	SDK_LIB_DIR = ../../lib/uclibc
else
	SDK_LIB_DIR = ../../lib/glibc
endif

SDK_INC_DIR = ../../include

INCLUDES = -I$(SDK_INC_DIR)

LIBS = $(SDK_LIB_DIR)/libsysutils.a $(SDK_LIB_DIR)/libalog.a

LDFLAG += -Wl,-gc-sections

COMMON_SRC := $(wildcard ../common/*.c)
COMMON_OBJ := $(COMMON_SRC:%.c=%.o)

SAMPLES_SRC := $(wildcard *.c)
SAMPLES_OBJ := $(SAMPLES_SRC:%.c=%.o)

SAMPLES := $(SAMPLES_OBJ:%.o=%)

all: 	$(SAMPLES)

$(SAMPLES):%:%.o $(COMMON_OBJ)
	$(CPLUSPLUS) $(LDFLAG) -o $@ $^ $(LIBS) -lpthread -lm -lrt
	$(STRIP) $@

%.o:%.c
	$(CC) -c $(CFLAGS) $< -o $@

clean:
	rm -f *.o *~

distclean: clean
	rm -f $(SAMPLES)
