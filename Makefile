TARGET	:= nbuffvdi

LIBCMINI ?= no

CC	:= m68k-atari-mint-gcc

CFLAGS	:= -O2 -fomit-frame-pointer -Wall -std=c99
LDFLAGS	:= -s
#LDFLAGS := -g -Wl,--traditional-format
LDLIBS  := -lgem 

ifeq ($(LIBCMINI),yes)
LIBCMINI_ROOT := $(shell $(CC) -print-sysroot)/opt/libcmini
CFLAGS	:= -I$(LIBCMINI_ROOT)/include $(CFLAGS)
LDFLAGS	:= -nostdlib $(LIBCMINI_ROOT)/lib/crt0.o $(LDFLAGS) -L$(LIBCMINI_ROOT)/lib
LDLIBS	:= -lcmini -lgcc $(LDLIBS)
endif

all: $(TARGET).ttp

$(TARGET).ttp: $(TARGET)
	cp $< $@

$(TARGET): $(TARGET).o

.PHONY: clean
clean:
	rm -f $(TARGET).ttp $(TARGET) *.o *~
