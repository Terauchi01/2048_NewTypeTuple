#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <deque>

using namespace std;

const int BOARD_SIZE = 16;

const int LOOKBACK_50  = 50;
const int LOOKBACK_100 = 100;

bool is_gameover_line(const string& line) {
    return line.find("gameover_turn:") != string::npos;
}

void print_board(const vector<int>& board) {

    for (int i = 0; i < BOARD_SIZE; i++) {

        if (board[i] == 0) {
            printf("%6d", 0);
        } else {
            printf("%6d", 1 << board[i]);
        }

        if (i % 4 == 3) printf("\n");
    }
}

void print_lookback(
    const deque<vector<int>>& recent_states,
    int lookback,
    int game_id
) {

    if ((int)recent_states.size() >= lookback) {

        vector<int> board =
            recent_states[recent_states.size() - lookback];

        cout << "====================================\n";
        cout << "GAME " << game_id
             << " : board at -" << lookback << " turns\n";
        cout << "====================================\n";

        print_board(board);
        cout << "\n";

    } else {

        cout << "GAME " << game_id
             << " : less than " << lookback
             << " turns\n\n";
    }
}

int main(int argc, char** argv) {

    if (argc != 2) {
        cerr << "Usage: " << argv[0] << " state.txt\n";
        return 1;
    }

    ifstream fin(argv[1]);

    if (!fin) {
        cerr << "Cannot open file\n";
        return 1;
    }

    deque<vector<int>> recent_states;

    string line;
    int game_id = 0;

    while (getline(fin, line)) {

        // gameover検出
        if (is_gameover_line(line)) {

            game_id++;

            // 50ターン前
            print_lookback(
                recent_states,
                LOOKBACK_50,
                game_id
            );

            // 100ターン前
            print_lookback(
                recent_states,
                LOOKBACK_100,
                game_id
            );

            recent_states.clear();
            continue;
        }

        // 通常盤面
        stringstream ss(line);

        vector<int> board;
        int x;

        while (ss >> x) {
            board.push_back(x);
        }

        if ((int)board.size() != BOARD_SIZE) continue;

        recent_states.push_back(board);

        // 最大100ターン分だけ保持
        if ((int)recent_states.size() > LOOKBACK_100 + 5) {
            recent_states.pop_front();
        }
    }

    return 0;
}