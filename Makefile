
BIN_DIR = bin/
OBJ_DIR = obj
INC_DIR = src/
SRC_DIR = src/

OBJS =     $(addprefix $(OBJ_DIR)/, ps2mcfs.o fat.o ecc.o)
INCLUDES = $(addprefix $(INC_DIR), ps2mcfs.h fat.h ecc.h vmc_types.h)

CC =     cc
CFLAGS = $(shell pkg-config fuse3 --cflags) -Wall -ggdb3 -O0 -std=gnu11 -D DEBUG=1
LIBS =   $(shell pkg-config fuse3 --libs)

.PHONY: clean all

all: .clang_complete $(BIN_DIR)/fuseps2mc $(BIN_DIR)/mkfs.ps2

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c $(INCLUDES) Makefile
	mkdir -p $(OBJ_DIR)
	$(CC) $(CFLAGS) -c "$<" -o "$@"

.clang_complete: Makefile
	echo "$(CFLAGS)" | tr " " "\n" > $@

clean:
	rm -f $(OBJS)

# executables

$(BIN_DIR)/fuseps2mc: $(OBJ_DIR)/main.o $(OBJS) $(INCLUDES) Makefile
	mkdir -p $(BIN_DIR)
	$(CC) $< $(OBJS) $(CFLAGS) $(LIBS) -o "$@"

$(BIN_DIR)/mkfs.ps2: $(OBJ_DIR)/mkfs_ps2.o $(OBJS) $(INCLUDES) Makefile
	mkdir -p $(BIN_DIR)
	$(CC) $< $(OBJS) $(CFLAGS) $(LIBS) -o "$@"
