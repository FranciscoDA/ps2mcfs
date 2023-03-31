
BIN_DIR = bin/
OBJ_DIR = bin/
INC_DIR = src/
SRC_DIR = src/

BIN =      $(addprefix $(BIN_DIR), fuseps2mc)
OBJS =     $(addprefix $(OBJ_DIR), ps2mcfs.o fat.o ecc.o main.o)
INCLUDES = $(addprefix $(INC_DIR), ps2mcfs.h fat.h ecc.h vmc_types.h)

CC =     cc
CFLAGS = $(shell pkg-config fuse3 --cflags) -Wall -ggdb3 -O0 -std=gnu11 -D DEBUG=1
LIBS =   $(shell pkg-config fuse3 --libs)

.PHONY: clean all

all: .clang_complete $(BIN)

$(OBJ_DIR)%.o: $(SRC_DIR)%.c $(INCLUDES) Makefile
	mkdir -p $(OBJ_DIR)
	$(CC) $(CFLAGS) -c "$<" -o "$@"

.clang_complete: Makefile
	echo "$(CFLAGS)" | tr " " "\n" > $@

clean:
	rm -f $(OBJS)

$(BIN): $(OBJS)
	mkdir -p $(BIN_DIR)
	$(CC) $(OBJS) $(CFLAGS) $(LIBS) -o $(BIN)

