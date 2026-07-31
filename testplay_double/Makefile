.phony: clean test help learn build-0 build-6 build-7 build-8 build-9 learn-6 learn-7 learn-8 learn-9 run-6 run-7 run-8 run-9 check-runs clean-logs stop-runs build-0-dg build-6-dg build-7-dg build-8-dg build-9-dg

# Detect OS name for conditional flags
UNAME_S := $(shell uname -s)

# コンパイラの選択：macOSではclang++、LinuxではG++
ifeq ($(UNAME_S),Darwin)
  CXX = clang++
  CFLAGS = -pthread -std=c++20 -W -Wall -O3 -I../head_double -Wconversion
  LFLAGS = -pthread -std=c++20
else
  CXX = g++
  CFLAGS = -pthread -std=c++2a -W -Wall -O3 -I../head_double -Wconversion
  LFLAGS = -pthread -std=c++2a
endif

ifeq ($(UNAME_S),Linux)
  CFLAGS += -mcmodel=large -march=native
  LFLAGS += -mcmodel=large -march=native
endif

default:
	echo "do nothing"

%.o:%.cpp
	$(CXX) $(CFLAGS) -c $<

# 依存関係
test_expectimax.o: test_expectimax
game2048_VSE.o: game2048_VSE.cpp ../head_double/game2048.h
tdplayer_VSE_symmetric.o: tdplayer_VSE_symmetric2.cpp ../head_double/tdplayer_VSE_symmetric2.h ../head_double/symmetric.h ../head_double/game2048.h ../head_double/util.h ../head_double/selected_6_tuples.h ../head_double/selected_7_tuples.h ../head_double/selected_8_tuples.h ../head_double/selected_9_tuples.h
symmetric.o: symmetric.cpp ../head_double/symmetric.h ../head_double/game2048.h
playgame.o: playgame.cpp ../head_double/game2048.h ../head_double/symmetric.h ../head_double/tdplayer2symmetric.h ../head_double/util.h
playgame_VSE.o: playgame_VSE.cpp ../head_double/game2048.h ../head_double/symmetric.h ../head_double/tdplayer_VSE_symmetric2.h ../head_double/util.h
game2048.o: game2048.cpp ../head_double/game2048.h

# 異なるタプルファイルでビルド
build-0:
	$(CXX) $(CFLAGS) -c test_expectimax.cpp -DTUPLE_FILE_TYPE=0
	$(CXX) $(CFLAGS) -c tdplayer_VSE_symmetric2.cpp -DTUPLE_FILE_TYPE=0
	$(CXX) $(CFLAGS) -c symmetric.cpp -DTUPLE_FILE_TYPE=0
	$(CXX) $(CFLAGS) -c game2048.cpp -DTUPLE_FILE_TYPE=0
	$(CXX) $(LFLAGS) -o test_expectimax_0 test_expectimax.o tdplayer_VSE_symmetric2.o symmetric.o game2048.o

build-6:
	$(CXX) $(CFLAGS) -c test_expectimax.cpp -DTUPLE_FILE_TYPE=6
	$(CXX) $(CFLAGS) -c tdplayer_VSE_symmetric2.cpp -DTUPLE_FILE_TYPE=6
	$(CXX) $(CFLAGS) -c symmetric.cpp -DTUPLE_FILE_TYPE=6
	$(CXX) $(CFLAGS) -c game2048.cpp -DTUPLE_FILE_TYPE=6
	$(CXX) $(LFLAGS) -o test_expectimax_6 test_expectimax.o tdplayer_VSE_symmetric2.o symmetric.o game2048.o

build-7:
	$(CXX) $(CFLAGS) -c test_expectimax.cpp -DTUPLE_FILE_TYPE=7
	$(CXX) $(CFLAGS) -c tdplayer_VSE_symmetric2.cpp -DTUPLE_FILE_TYPE=7
	$(CXX) $(CFLAGS) -c symmetric.cpp -DTUPLE_FILE_TYPE=7
	$(CXX) $(CFLAGS) -c game2048.cpp -DTUPLE_FILE_TYPE=7
	$(CXX) $(LFLAGS) -o test_expectimax_7 test_expectimax.o tdplayer_VSE_symmetric2.o symmetric.o game2048.o

build-8:
	$(CXX) $(CFLAGS) -c test_expectimax.cpp -DTUPLE_FILE_TYPE=8
	$(CXX) $(CFLAGS) -c tdplayer_VSE_symmetric2.cpp -DTUPLE_FILE_TYPE=8
	$(CXX) $(CFLAGS) -c symmetric.cpp -DTUPLE_FILE_TYPE=8
	$(CXX) $(CFLAGS) -c game2048.cpp -DTUPLE_FILE_TYPE=8
	$(CXX) $(LFLAGS) -o test_expectimax_8 test_expectimax.o tdplayer_VSE_symmetric2.o symmetric.o game2048.o

build-9:
	$(CXX) $(CFLAGS) -c test_expectimax.cpp -DTUPLE_FILE_TYPE=9
	$(CXX) $(CFLAGS) -c tdplayer_VSE_symmetric2.cpp -DTUPLE_FILE_TYPE=9
	$(CXX) $(CFLAGS) -c symmetric.cpp -DTUPLE_FILE_TYPE=9
	$(CXX) $(CFLAGS) -c game2048.cpp -DTUPLE_FILE_TYPE=9
	$(CXX) $(LFLAGS) -o test_expectimax_9 test_expectimax.o tdplayer_VSE_symmetric2.o symmetric.o game2048.o

build-0-dg:
	$(CXX) $(CFLAGS) -c test_expectimax.cpp -DTUPLE_FILE_TYPE=0 -DDOWNGRADING
	$(CXX) $(CFLAGS) -c tdplayer_VSE_symmetric2.cpp -DTUPLE_FILE_TYPE=0 -DDOWNGRADING
	$(CXX) $(CFLAGS) -c symmetric.cpp -DTUPLE_FILE_TYPE=0 -DDOWNGRADING
	$(CXX) $(CFLAGS) -c game2048.cpp -DTUPLE_FILE_TYPE=0 -DDOWNGRADING
	$(CXX) $(LFLAGS) -o test_expectimax_0_dg test_expectimax.o tdplayer_VSE_symmetric2.o symmetric.o game2048.o

build-6-dg:
	$(CXX) $(CFLAGS) -c test_expectimax.cpp -DTUPLE_FILE_TYPE=6 -DDOWNGRADING
	$(CXX) $(CFLAGS) -c tdplayer_VSE_symmetric2.cpp -DTUPLE_FILE_TYPE=6 -DDOWNGRADING
	$(CXX) $(CFLAGS) -c symmetric.cpp -DTUPLE_FILE_TYPE=6 -DDOWNGRADING
	$(CXX) $(CFLAGS) -c game2048.cpp -DTUPLE_FILE_TYPE=6 -DDOWNGRADING
	$(CXX) $(LFLAGS) -o test_expectimax_6_dg test_expectimax.o tdplayer_VSE_symmetric2.o symmetric.o game2048.o

build-7-dg:
	$(CXX) $(CFLAGS) -c test_expectimax.cpp -DTUPLE_FILE_TYPE=7 -DDOWNGRADING
	$(CXX) $(CFLAGS) -c tdplayer_VSE_symmetric2.cpp -DTUPLE_FILE_TYPE=7 -DDOWNGRADING
	$(CXX) $(CFLAGS) -c symmetric.cpp -DTUPLE_FILE_TYPE=7 -DDOWNGRADING
	$(CXX) $(CFLAGS) -c game2048.cpp -DTUPLE_FILE_TYPE=7 -DDOWNGRADING
	$(CXX) $(LFLAGS) -o test_expectimax_7_dg test_expectimax.o tdplayer_VSE_symmetric2.o symmetric.o game2048.o

build-8-dg:
	$(CXX) $(CFLAGS) -c test_expectimax.cpp -DTUPLE_FILE_TYPE=8 -DDOWNGRADING
	$(CXX) $(CFLAGS) -c tdplayer_VSE_symmetric2.cpp -DTUPLE_FILE_TYPE=8 -DDOWNGRADING
	$(CXX) $(CFLAGS) -c symmetric.cpp -DTUPLE_FILE_TYPE=8 -DDOWNGRADING
	$(CXX) $(CFLAGS) -c game2048.cpp -DTUPLE_FILE_TYPE=8 -DDOWNGRADING
	$(CXX) $(LFLAGS) -o test_expectimax_8_dg test_expectimax.o tdplayer_VSE_symmetric2.o symmetric.o game2048.o

build-9-dg:
	$(CXX) $(CFLAGS) -c test_expectimax.cpp -DTUPLE_FILE_TYPE=9 -DDOWNGRADING
	$(CXX) $(CFLAGS) -c tdplayer_VSE_symmetric2.cpp -DTUPLE_FILE_TYPE=9 -DDOWNGRADING
	$(CXX) $(CFLAGS) -c symmetric.cpp -DTUPLE_FILE_TYPE=9 -DDOWNGRADING
	$(CXX) $(CFLAGS) -c game2048.cpp -DTUPLE_FILE_TYPE=9 -DDOWNGRADING
	$(CXX) $(LFLAGS) -o test_expectimax_9_dg test_expectimax.o tdplayer_VSE_symmetric2.o symmetric.o game2048.o
