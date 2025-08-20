#include "../head/game2048.h"
#include "../head/tdplayer_VSE_symmetric2.h"

// Provide the older signature expected by game2048_VSE.cpp
// This wrapper simply calls the zero-argument init_tuple() implemented in tdplayer.
void init_tuple(char** argv) {
    (void)argv; // ignore argv
    init_tuple();
}
