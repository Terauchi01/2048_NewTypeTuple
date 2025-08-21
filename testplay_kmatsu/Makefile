.phony: clean test help learn build-6 build-7 build-8 build-9 learn-6 learn-7 learn-8 learn-9 run-6 run-7 run-8 run-9 check-runs clean-logs stop-runs

# Detect OS name for conditional flags
UNAME_S := $(shell uname -s)

# コンパイラの選択：macOSではclang++、LinuxではG++
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

default:
	echo "do nothing"

%.o:%.cpp
	$(CXX) $(CFLAGS) -c $<

# 依存関係
test_expectimax.o: test_expectimax
game2048_VSE.o: game2048_VSE.cpp ../head/game2048.h
tdplayer_VSE_symmetric.o: tdplayer_VSE_symmetric2.cpp ../head/tdplayer_VSE_symmetric2.h ../head/symmetric.h ../head/game2048.h ../head/util.h ../head/selected_6_tuples.h ../head/selected_7_tuples.h ../head/selected_8_tuples.h ../head/selected_9_tuples.h
symmetric.o: symmetric.cpp ../head/symmetric.h ../head/game2048.h
playgame.o: playgame.cpp ../head/game2048.h ../head/symmetric.h ../head/tdplayer2symmetric.h ../head/util.h
playgame_VSE.o: playgame_VSE.cpp ../head/game2048.h ../head/symmetric.h ../head/tdplayer_VSE_symmetric2.h ../head/util.h
game2048.o: game2048.cpp ../head/game2048.h

# 異なるタプルファイルでビルド
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
