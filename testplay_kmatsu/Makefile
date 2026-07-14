.PHONY: clean default \
	build-0 build-6 build-7 build-8 build-9 \
	build-log-0 build-log-6 build-log-7 build-log-8 build-log-9

# =========================================================
# OS detection
# =========================================================
UNAME_S := $(shell uname -s)

ifeq ($(UNAME_S),Darwin)
	CXX = clang++
	CFLAGS = -pthread -std=c++20 -W -Wall -O3 -I../head
	LFLAGS = -pthread -std=c++20
else
	CXX = g++
	CFLAGS = -pthread -std=c++2a -W -Wall -O3 -I../head
	LFLAGS = -pthread -std=c++2a
endif

ifeq ($(UNAME_S),Linux)
	CFLAGS += -mcmodel=large -march=native
	LFLAGS += -mcmodel=large -march=native
endif

# debug
CFLAGS += -DDEBUG_PLAY

default:
	@echo "usage:"
	@echo "  make build-6"
	@echo "  make build-log-6"

# =========================================================
# normal expectimax build
# =========================================================

build-0:
	$(MAKE) build TARGET=0 SRC=test_expectimax.cpp OUT=test_expectimax_0

build-6:
	$(MAKE) build TARGET=6 SRC=test_expectimax.cpp OUT=test_expectimax_6

build-7:
	$(MAKE) build TARGET=7 SRC=test_expectimax.cpp OUT=test_expectimax_7

build-8:
	$(MAKE) build TARGET=8 SRC=test_expectimax.cpp OUT=test_expectimax_8

build-9:
	$(MAKE) build TARGET=9 SRC=test_expectimax.cpp OUT=test_expectimax_9

# =========================================================
# withlog build
# =========================================================

build-log-0:
	$(MAKE) build TARGET=0 SRC=test_expectimax_withlog.cpp OUT=test_expectimax_withlog_0

build-log-6:
	$(MAKE) build TARGET=6 SRC=test_expectimax_withlog.cpp OUT=test_expectimax_withlog_6

build-log-7:
	$(MAKE) build TARGET=7 SRC=test_expectimax_withlog.cpp OUT=test_expectimax_withlog_7

build-log-8:
	$(MAKE) build TARGET=8 SRC=test_expectimax_withlog.cpp OUT=test_expectimax_withlog_8

build-log-9:
	$(MAKE) build TARGET=9 SRC=test_expectimax_withlog.cpp OUT=test_expectimax_withlog_9

# =========================================================
# common build rule
# =========================================================

build:
	$(CXX) $(CFLAGS) -DTUPLE_FILE_TYPE=$(TARGET) -c $(SRC) -o main_$(TARGET).o

	$(CXX) $(CFLAGS) -DTUPLE_FILE_TYPE=$(TARGET) \
		-c tdplayer_VSE_symmetric2.cpp \
		-o tdplayer_$(TARGET).o

	$(CXX) $(CFLAGS) -DTUPLE_FILE_TYPE=$(TARGET) \
		-c symmetric.cpp \
		-o symmetric_$(TARGET).o

	$(CXX) $(CFLAGS) -DTUPLE_FILE_TYPE=$(TARGET) \
		-c game2048.cpp \
		-o game2048_$(TARGET).o

	$(CXX) $(LFLAGS) \
		-o $(OUT) \
		main_$(TARGET).o \
		tdplayer_$(TARGET).o \
		symmetric_$(TARGET).o \
		game2048_$(TARGET).o

# =========================================================
# clean
# =========================================================

clean:
	rm -f *.o
	rm -f test_expectimax_*
	rm -f test_expectimax_withlog_*