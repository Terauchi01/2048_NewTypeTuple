// g++ analyze_tile_reach_turn.cpp \
//     -O3 \
//     -march=native \
//     -fopenmp \
//     -o analyze_tile_reach_turn
#include <iostream>
#include <fstream>
#include <vector>
#include <array>
#include <string>
#include <filesystem>
#include <omp.h>

using namespace std;

const string BASE =
    "/home2/matsu-lab2/terauchi/2048_NewTypeTuple/board_data";


// 1ファイル分解析
array<vector<int>, 32> analyze_file(const string& path)
{
    array<vector<int>, 32> reach;

    ifstream file(path, ios::in | ios::binary);

    if (!file)
        return reach;


    string line;
    array<bool, 32> seen{};
    int turn = 0;


    while (getline(file, line))
    {
        if (line.empty())
            continue;


        if (line[0] == 'g')
        {
            turn = 0;
            seen.fill(false);
            continue;
        }


        turn++;


        int num = 0;

        // 高速整数パース
        for (size_t i = 0; i <= line.size(); i++)
        {
            if (i < line.size() && line[i] >= '0' && line[i] <= '9')
            {
                num = num * 10 + (line[i] - '0');
            }
            else
            {
                if (num > 0 && num < 32 && !seen[num])
                {
                    seen[num] = true;
                    reach[num].push_back(turn);
                }

                num = 0;
            }
        }
    }


    return reach;
}



int main()
{

    for (int nt : {6,7,8,9})
    {

        cout << "\n===== NT" << nt << " =====\n";


        array<vector<int>,32> total;


        // seedごとに独立
        #pragma omp parallel
        {

            array<vector<int>,32> local;


            #pragma omp for
            for (int seed=0; seed<5; seed++)
            {

                string path =
                    BASE +
                    "/EXP_1-NT" +
                    to_string(nt) +
                    "-TN" +
                    to_string(nt) +
                    "-OI0-seed" +
                    to_string(seed) +
                    "/after_state.txt";


                auto result = analyze_file(path);


                for(int e=0;e<32;e++)
                {
                    local[e].insert(
                        local[e].end(),
                        result[e].begin(),
                        result[e].end()
                    );
                }
            }


            // merge
            #pragma omp critical
            {
                for(int e=0;e<32;e++)
                {
                    total[e].insert(
                        total[e].end(),
                        local[e].begin(),
                        local[e].end()
                    );
                }
            }
        }



        for(int exp=0;exp<32;exp++)
        {
            auto &data = total[exp];

            if(data.empty())
                continue;


            long long sum = 0;
            int mn = data[0];
            int mx = data[0];


            for(int x:data)
            {
                sum += x;
                mn = min(mn,x);
                mx = max(mx,x);
            }


            cout
                << "2^"
                << exp
                << " ("
                << (1<<exp)
                << ") : "
                << "mean="
                << (double)sum/data.size()
                << ", min="
                << mn
                << ", max="
                << mx
                << ", count="
                << data.size()
                << "\n";
        }

    }


    return 0;
}