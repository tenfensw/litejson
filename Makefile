CC ?= cc
CFLAGS := -Wall -std=c99 -I. $(CFLAGS)

ifndef RELEASE
CFLAGS := -g $(CFLAGS) -DDEBUG=1 -DLJ_DEBUG_ALLOW_COLORS=1
endif

TARGET = litejson
TARGETS = litejson.o main.o

all: $(TARGET)

$(TARGET): $(TARGETS)
	$(CC) -o $(TARGET) $(TARGETS)

$(TARGETS):
	$(CC) -c -o "$@" $(CFLAGS) $(shell basename "$@" .o).c

clean: distclean

distclean:
	-rm -rf $(TARGET) $(TARGETS) *.dSYM
