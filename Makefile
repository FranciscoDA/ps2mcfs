
BIN_DIR = bin
OBJ_DIR = obj
INC_DIR = src
SRC_DIR = src

OBJS =     $(addprefix $(OBJ_DIR)/, ps2mcfs.o fat.o ecc.o mc_writer.o)
INCLUDES = $(addprefix $(INC_DIR)/, ps2mcfs.h fat.h ecc.h vmc_types.h utils.h)

TEST_OBJS = $(addprefix $(OBJ_DIR)/, munit.o)
TEST_INCLUDES = vendor/munit/munit.h

CC =     cc
CFLAGS = $(shell pkg-config fuse3 --cflags) -I./vendor -Wall -ggdb3 -O0 -std=gnu11 -D DEBUG=1
LIBS =   $(shell pkg-config fuse3 --libs)

.PHONY: clean all

all: .clang_complete $(BIN_DIR)/fuseps2mc $(BIN_DIR)/mkfs.ps2 $(BIN_DIR)/tests

$(OBJ_DIR)/munit.o: vendor/munit/munit.c vendor/munit/munit.h
	mkdir -p $(OBJ_DIR)
	$(CC) $(CFLAGS) -c "$<" -o "$@"

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c $(INCLUDES) Makefile
	mkdir -p $(OBJ_DIR)
	$(CC) $(CFLAGS) -c "$<" -o "$@"

.clang_complete: Makefile
	echo "$(CFLAGS)" | tr " " "\n" > $@

clean:
	rm -f $(OBJS)

# vendor dependencies
vendor/munit/%:
	git submodule update -f -- vendor/munit/

# executables

$(BIN_DIR)/fuseps2mc: $(OBJ_DIR)/fuseps2mc.o $(OBJS) $(INCLUDES) Makefile
	mkdir -p $(BIN_DIR)
	$(CC) $< $(OBJS) $(CFLAGS) $(LIBS) -o "$@"

$(BIN_DIR)/mkfs.ps2: $(OBJ_DIR)/mkfs_ps2.o $(OBJS) $(INCLUDES) Makefile
	mkdir -p $(BIN_DIR)
	$(CC) $< $(OBJS) $(CFLAGS) $(LIBS) -o "$@"

$(BIN_DIR)/tests: $(OBJ_DIR)/tests.o $(OBJS) $(TEST_OBJS) $(INCLUDES) $(TEST_INCLUDES)
	mkdir -p $(BIN_DIR)
	$(CC) $< $(OBJS) $(TEST_OBJS) $(CFLAGS) $(LIBS) -o "$@"