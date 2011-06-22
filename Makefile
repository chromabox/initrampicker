#
# initrampicker Makefile
#  initramfs(cpio) taken from kernel zImage
#   written by chromabox
#
#

# build tools
CC := $(CROSS_COMPILE)gcc
LD := $(CROSS_COMPILE)ld

TARGET    := initrampicker
OBJS      := initrampicker.o

CFLAGS     = -O0 -Wall
# CFLAGS     = -O0 -Wall -g D_DEBUG

all: $(TARGET)

clean: 
	rm -f *.o $(TARGET) *~ 

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

initrampicker.o: initrampicker.c
