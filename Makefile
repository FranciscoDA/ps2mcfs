
OBJS = ps2mcfs.o fuseps2mc.o fat.o
INCLUDES = ps2mcfs.h fat.h
CC = gcc
CFLAGS = $(shell pkg-config fuse3 --cflags) -Wall -ggdb
LIBS = $(shell pkg-config fuse3 --libs)
BIN = fuseps2mc

all: $(BIN)

%.o: %.c $(INCLUDES)
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS)

$(BIN): $(OBJS)
	$(CC) $(OBJS) $(CFLAGS) $(LIBS) -o $(BIN)

