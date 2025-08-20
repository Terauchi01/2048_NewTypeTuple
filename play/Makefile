.PHONY: all clean help build-6 build-7 build-8 build-9 run

# Simple Makefile to build play_greedy executable in play/
# Based on learn/Makefile_VSE

# Default tuple type
TUPLE_TYPE ?= 6

# Detect OS
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
  CXX = clang++
else
  CXX = g++
endif

CFLAGS = -std=c++20 -O2 -I../head -pthread -W -Wall
LFLAGS = -std=c++20 -O2 -pthread
ifeq ($(UNAME_S),Linux)
  CFLAGS += -mcmodel=large -march=native
  LFLAGS += -mcmodel=large -march=native
endif

TARGET = play_greedy

# Sources (paths are relative to play/)
SRC = play_greedy_2048.cpp
TD_SRC = ../learn/tdplayer_VSE_symmetric2.cpp
GAME_SRC = ../learn/game2048_VSE.cpp
SYM_SRC = ../learn/symmetric.cpp
WRAP_SRC = init_tuple_wrapper.cpp

OBJS = $(patsubst %.cpp,%.o,$(SRC)) tdplayer_VSE_symmetric2.o game2048_VSE.o symmetric.o init_tuple_wrapper.o

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(LFLAGS) -o $@ $^

# Compile play_greedy_2048.cpp into play_greedy_2048.o
play_greedy_2048.o: play_greedy_2048.cpp
	$(CXX) $(CFLAGS) -c $< -o $@

# Compile tdplayer with selectable tuple type
tdplayer_VSE_symmetric2.o: $(TD_SRC)
	$(CXX) $(CFLAGS) -c $(TD_SRC) -DTUPLE_FILE_TYPE=$(TUPLE_TYPE) -o $@

game2048_VSE.o: $(GAME_SRC)
	$(CXX) $(CFLAGS) -c $(GAME_SRC) -o $@

symmetric.o: $(SYM_SRC)
	$(CXX) $(CFLAGS) -c $(SYM_SRC) -o $@

init_tuple_wrapper.o: $(WRAP_SRC)
	$(CXX) $(CFLAGS) -c $(WRAP_SRC) -o $@

# Convenience targets for building with different tuple types
build-6:
	$(MAKE) clean
	$(MAKE) TUPLE_TYPE=6 all
	mv $(TARGET) $(TARGET)_6

build-7:
	$(MAKE) clean
	$(MAKE) TUPLE_TYPE=7 all
	mv $(TARGET) $(TARGET)_7

build-8:
	$(MAKE) clean
	$(MAKE) TUPLE_TYPE=8 all
	mv $(TARGET) $(TARGET)_8

build-9:
	$(MAKE) clean
	$(MAKE) TUPLE_TYPE=9 all
	mv $(TARGET) $(TARGET)_9

# Run target: ./play_greedy <seed> <game_counts> <evfile>
run: $(TARGET)
	@echo "Usage: ./$(TARGET) <seed> <game_counts> <evfile>"

clean:
	rm -f $(TARGET) $(TARGET)_6 $(TARGET)_7 $(TARGET)_8 $(TARGET)_9 *.o

help:
	@echo "Makefile for play/play_greedy_2048.cpp"
	@echo ""
	@echo "Available targets:"
	@echo "  all            - Build the default executable (TUPLE_TYPE=$(TUPLE_TYPE))"
	@echo "  build-6        - Build with 6-tuples (output: $(TARGET)_6)"
	@echo "  build-7        - Build with 7-tuples (output: $(TARGET)_7)"
	@echo "  build-8        - Build with 8-tuples (output: $(TARGET)_8)"
	@echo "  build-9        - Build with 9-tuples (output: $(TARGET)_9)"
	@echo "  run            - Print usage for running the produced binary"
	@echo "  clean          - Remove binary and object files"
	@echo "  help           - Show this help message"
	@echo ""
	@echo "Examples:"
	@echo "  make            # Build with default tuple type (6)"
	@echo "  make build-8    # Build with 8-tuples -> $(TARGET)_8"
	@echo "  make TUPLE_TYPE=7  # Build with 7-tuples (alternate style)"
	@echo "  ./$(TARGET)_9 <seed> <game_counts> <evfile>  # Run the 9-tuple player"
