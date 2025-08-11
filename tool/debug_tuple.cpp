#include <iostream>
#include <set>
#define TUPLE_SIZE 9
#include "selected_9_tuples.h"
// index番目のタプルを出力
void print_tuple_board(int index) {
    if (index < 0 || index >= AVAIL_TUPLE) {
        std::cout << "Invalid index" << std::endl;
        return;
    }
    std::set<int> tuple_cells;
    for (int i = 0; i < TUPLE_SIZE; ++i) tuple_cells.insert(tuples[index][i]);
    for (int y = 0; y < 4; ++y) {
        for (int x = 0; x < 4; ++x) {
            int cell = y * 4 + x;
            if (tuple_cells.count(cell))
                std::cout << "* ";
            else
                std::cout << ". ";
        }
        std::cout << std::endl;
    }
    std::cout << std::endl;
}

int main() {
    for(int i = 0; i < AVAIL_TUPLE; i++)
        print_tuple_board(i);
}