
BIN_DIR = bin/
OBJ_DIR = bin/
INC_DIR = src/
SRC_DIR = src/

BIN =      $(addprefix $(BIN_DIR), fuseps2mc)
OBJS =     $(addprefix $(OBJ_DIR), ps2mcfs.o fuseps2mc.o fat.o)
INCLUDES = $(addprefix $(INC_DIR), ps2mcfs.h fat.h)

CC =     clang
CFLAGS = $(shell pkg-config fuse3 --cflags) -Wall -ggdb -std=gnu11
LIBS =   $(shell pkg-config fuse3 --libs)

.PHONY: clean all

all: .clang_complete $(BIN)

$(OBJ_DIR)%.o: $(SRC_DIR)%.c $(INCLUDES)
	mkdir -p $(OBJ_DIR)
	$(CC) $(CFLAGS) -c "$<" -o "$@"

.clang_complete: Makefile
	tr " " "\n" <<<"$(CFLAGS)" > $@

clean:
	rm -f $(OBJS)

$(BIN): $(OBJS)
	mkdir -p $(BIN_DIR)
	$(CC) $(OBJS) $(CFLAGS) $(LIBS) -o $(BIN)

