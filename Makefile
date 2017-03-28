
OBJS = ps2mcfs.o fuseps2mc.o fat.o
INCLUDES = ps2mcfs.h fat.h
CC = clang
CFLAGS = $(shell pkg-config fuse3 --cflags) -Wall -ggdb -std=gnu11
LIBS = $(shell pkg-config fuse3 --libs)
BIN = fuseps2mc

all: .clang_complete $(BIN)

%.o: %.c $(INCLUDES)
	$(CC) $(CFLAGS) -c $< -o $@

.clang_complete: Makefile
	tr " " "\n" <<<"$(CFLAGS)" > $@

clean:
	rm -f $(OBJS)

$(BIN): $(OBJS)
	$(CC) $(OBJS) $(CFLAGS) $(LIBS) -o $(BIN)

