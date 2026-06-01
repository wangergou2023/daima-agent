include ../Makefile.param

COMMON_SRC := $(wildcard ../common/*.c)
COMMON_OBJ := $(COMMON_SRC:%.c=%.o)

SAMPLES_SRC := $(wildcard *.c)
SAMPLES_OBJ := $(SAMPLES_SRC:%.c=%.o)

SAMPLES := $(SAMPLES_OBJ:%.o=%)

all: 	$(SAMPLES)

$(SAMPLES):%:%.o $(COMMON_OBJ)
	$(CPLUSPLUS) $(LDFLAG) -o $@ $^ $(LIBS) $(SDK_LIB_DIR)/libsysutils.a  -lpthread -lm -lrt
	$(STRIP) $@

%.o:%.c sample-common.h
	$(CC) -c $(CFLAGS) $< -o $@

clean:
	rm -f *.o *~; rm -f ../common/*.o

distclean: clean
	rm -f $(SAMPLES)
