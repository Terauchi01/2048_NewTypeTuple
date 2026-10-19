.PHONY: clean default \
	build-0 build-6 build-7 build-8 build-9 \
	build-log-0 build-log-6 build-log-7 build-log-8 build-log-9 \
	beam build-beam build-beam-6 test-beam test-beam-afterstate \
	build-beam-afterstate-6

# Beam Search experiments use the requested 6-tuple evaluator by default.
TARGET ?= 6

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

BEAM_CFLAGS := $(CFLAGS)
BEAM_LFLAGS := $(LFLAGS)

# debug
CFLAGS += -DDEBUG_PLAY

default:
	@echo "usage:"
	@echo "  make beam        # build 6-tuple Beam Search runner"
	@echo "  make test-beam   # run evaluator-independent unit tests"
	@echo "  make build-6"
	@echo "  make build-log-6"

# =========================================================
# Beam Search (6-tuple)
# =========================================================

beam: build-beam-6

build-beam-6:
	$(MAKE) build-beam TARGET=6 OUT=test_beam_search_6

build-beam:
	$(CXX) $(BEAM_CFLAGS) -DTUPLE_FILE_TYPE=$(TARGET) \
		test_beam_search.cpp beam_search.cpp double_evaluator.cpp game2048.cpp \
		$(BEAM_LFLAGS) -o $(OUT)

test-beam:
	$(CXX) $(CFLAGS) -UDEBUG_PLAY -DTUPLE_FILE_TYPE=6 \
		test_beam_search_unit.cpp beam_search.cpp double_evaluator.cpp game2048.cpp \
		$(LFLAGS) -o beam_search_unit_tests
	./beam_search_unit_tests

build-beam-afterstate-6:
	$(CXX) $(BEAM_CFLAGS) -DTUPLE_FILE_TYPE=6 \
		test_beam_search_afterstate_runner.cpp beam_search_afterstate.cpp \
		beam_search.cpp double_evaluator.cpp game2048.cpp \
		$(BEAM_LFLAGS) -o test_beam_search_afterstate_6

test-beam-afterstate:
	$(CXX) $(CFLAGS) -UDEBUG_PLAY -DTUPLE_FILE_TYPE=6 \
		test_beam_search_afterstate.cpp beam_search_afterstate.cpp \
		beam_search.cpp game2048.cpp $(LFLAGS) -o beam_search_afterstate_tests
	./beam_search_afterstate_tests

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
	rm -f test_expectimax_0 test_expectimax_6 test_expectimax_7 test_expectimax_8 test_expectimax_9
	rm -f test_expectimax_withlog_0 test_expectimax_withlog_6 test_expectimax_withlog_7 test_expectimax_withlog_8 test_expectimax_withlog_9
	rm -f test_beam_search_6 beam_search_unit_tests beam_search_afterstate_tests
	rm -f test_beam_search_afterstate_6
