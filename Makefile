#
# Makefile
#
CC = clang
CFLAGS += -std=c89 -pedantic-errors -Wall -Wextra -Wno-unused-parameter \
          -g -O2 -march=native \
          -D_POSIX_C_SOURCE=200112L -D_XOPEN_SOURCE=600L \
          -D_FILE_OFFSET_BITS=64 -D_FORTIFY_SOURCE=2
INCLUDES += -I ./include
LIBS += -lixp
TARGET = unpfs
OBJS = src/common.o \
       src/fid.o \
       src/posix.o \
       src/handler.o \
       src/ops.o \
       src/log.o \
       src/unpfs.o

$(TARGET): $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $(OBJS) $(LIBS)

.c.o:
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

clean:
	-rm -f $(TARGET) $(OBJS)
