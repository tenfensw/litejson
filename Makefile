CC ?= cc
CFLAGS := -Wall -std=c99 -I. $(CFLAGS)

AR ?= ar

ifndef RELEASE
CFLAGS := -g $(CFLAGS) -DDEBUG=1 -DLJ_DEBUG_ALLOW_COLORS=1
endif

TARGET = litejson
LIB_TARGET = liblitejson.a

TARGETS = main.o
LIB_TARGETS = litejson.o

all: lib cli

lib: $(LIB_TARGET)
cli: $(TARGET)

$(TARGET): $(LIB_TARGET) $(TARGETS)
	$(CC) -o $(TARGET) $(TARGETS) -L. -llitejson

$(LIB_TARGET): $(LIB_TARGETS)
	$(AR) crs $(LIB_TARGET) $(LIB_TARGETS)

$(TARGETS) $(LIB_TARGETS):
	$(CC) -c -o "$@" $(CFLAGS) $(shell basename "$@" .o).c

clean: distclean

distclean:
	-rm -rf $(TARGET) $(LIB_TARGET) $(TARGETS) $(LIB_TARGETS) *.dSYM
