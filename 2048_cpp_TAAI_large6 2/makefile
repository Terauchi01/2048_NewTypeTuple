.phony: clean

TARGET = playgame

$(TARGET): playgame.o tdplayer2symmetric.o symmetric.o game2048.o
	g++ -pthread -std=c++11 -mcmodel=large -o $@ $+

%.o:%.cpp
	g++ -pthread -std=c++11 -c -W -Wall -O3 -mcmodel=large $<

playgame.o: playgame.cpp tdplayer2symmetric.h symmetric.h game2048.h
tdplayer2symmetric.o: tdplayer2symmetric.cpp tdplayer2symmetric.h symmetric.h game2048.h
symmetric.o: symmetric.cpp symmetric.h game2048.h
game2048.o: game2048.cpp game2048.h

clean:
	rm -f $(TARGET)
	rm -f *.o
	rm -f *~
