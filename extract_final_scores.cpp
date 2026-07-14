#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <cstdio>

using namespace std;
namespace fs = std::filesystem;

constexpr long long MAX_STEPS = 200000000000LL;

int main()
{
    ofstream out("first_end_scores.csv");

    out << "tuple,seed,game,steps,score,big,turn\n";

    const string logdir = "learn_double/logs";

    for (auto &entry : fs::directory_iterator(logdir))
    {
        string filename = entry.path().filename().string();

        int tuple;
        int seed;

        if (sscanf(filename.c_str(),
                   "%dtuple_seed%d.log",
                   &tuple,
                   &seed) != 2)
            continue;

        ifstream fin(entry.path());

        string line;

        while (getline(fin, line))
        {
            if (line.rfind("first_end,game,", 0) != 0)
                continue;

            int game;
            int score;
            int big;
            int turn;
            long long steps;

            if (sscanf(
                    line.c_str(),
                    "first_end,game,%d,sco,%d,big,%d,turn,%d,steps,%lld",
                    &game,
                    &score,
                    &big,
                    &turn,
                    &steps) != 5)
                continue;

            // 指定学習ターンまでで打ち切る
            if (steps > MAX_STEPS)
                break;

            out
                << tuple << ","
                << seed << ","
                << game << ","
                << steps << ","
                << score << ","
                << big << ","
                << turn << "\n";
        }

        cout << "finished " << filename << endl;
    }

    out.close();

    cout << "saved first_end_scores.csv" << endl;

    return 0;
}