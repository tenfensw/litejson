CC ?= cc
CFLAGS := -Wall -std=c99 -I. $(CFLAGS)

AR ?= ar

ifndef RELEASE
CFLAGS := -g $(CFLAGS) -DDEBUG=1 -DLJ_DEBUG_ALLOW_COLORS=1
else
CFLAGS := -Os $(CFLAGS)
endif

CLI_TARGET = jsonedit
LIB_TARGET = liblitejson.a

CLI_TARGETS = jsonedit.o
LIB_TARGETS = litejson.o

all: lib cli

lib: $(LIB_TARGET)
cli: $(CLI_TARGET)

$(CLI_TARGET): $(LIB_TARGET) $(CLI_TARGETS)
	$(CC) -o $(CLI_TARGET) $(CLI_TARGETS) -L. -llitejson

$(LIB_TARGET): $(LIB_TARGETS)
	$(AR) crs $(LIB_TARGET) $(LIB_TARGETS)

$(CLI_TARGETS) $(LIB_TARGETS):
	$(CC) -c -o "$@" $(CFLAGS) $(shell basename "$@" .o).c

clean: distclean

distclean:
	-rm -rf $(CLI_TARGET) $(LIB_TARGET) $(CLI_TARGETS) $(LIB_TARGETS) *.dSYM
