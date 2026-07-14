#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <mutex>
#include <string>
#include <tuple>
#include <thread>
#include <vector>

using namespace std;
namespace fs = std::filesystem;

struct TurnStat
{
    long long count = 0;
    long long straight = 0;
    long long block2x2 = 0;
    double logTile[16] = {};
    double top3LogTile[16] = {};
};

struct GameStat
{
    long long count = 0;
    long long straight = 0;
    long long block2x2 = 0;
};

using TurnStats = map<int, map<int, map<int, TurnStat>>>;
auto& gs = (*gameStats)[makeGameKey(seed, turn, game)][tupleIndex(tuple)];

struct FileTask
{
    int tuple;
    string path;
};

void addTurnStat(TurnStat& dst, const TurnStat& src)
{
    dst.count += src.count;
    dst.straight += src.straight;
    dst.block2x2 += src.block2x2;

    for (int i = 0; i < 16; i++)
    {
        dst.logTile[i] += src.logTile[i];
        dst.top3LogTile[i] += src.top3LogTile[i];
    }
}

void addGameStat(GameStat& dst, const GameStat& src)
{
    dst.count += src.count;
    dst.straight += src.straight;
    dst.block2x2 += src.block2x2;
}

tuple<int, int, int> makeGameKey(int seed, int turn, int game)
{
    return {seed, turn, game};
}

void mergeTurnStats(TurnStats& dst, const TurnStats& src)
{
    for (const auto& [seed, turnMap] : src)
    {
        for (const auto& [turn, tupleMap] : turnMap)
        {
            for (const auto& [tuple, stat] : tupleMap)
            {
                addTurnStat(dst[seed][turn][tuple], stat);
            }
        }
    }
}

void mergeGameStats(GameStats& dst, const GameStats& src)
{
    for (const auto& [key, stat] : src)
    {
        addGameStat(dst[key], stat);
    }
}

struct GamePartialRow
{
    tuple<int, int, int> key;
    string tail;
    bool valid = false;
};

bool loadGamePartialRow(ifstream& in, GamePartialRow& row)
{
    string line;
    while (getline(in, line))
    {
        if (line.empty() || line.rfind("seed,game,turn", 0) == 0)
            continue;

        size_t first = line.find(',');
        if (first == string::npos)
            continue;
        size_t second = line.find(',', first + 1);
        if (second == string::npos)
            continue;
        size_t third = line.find(',', second + 1);
        if (third == string::npos)
            continue;

        try
        {
            int seed = stoi(line.substr(0, first));
            int game = stoi(line.substr(first + 1, second - first - 1));
            int turn = stoi(line.substr(second + 1, third - second - 1));
            row.key = {seed, turn, game};
            row.tail = line.substr(third + 1);
            row.valid = true;
            return true;
        }
        catch (...)
        {
            continue;
        }
    }

    row.valid = false;
    return false;
}

void writeMergedGameCsv(const vector<string>& partialFiles)
{
    struct Reader
    {
        ifstream in;
        GamePartialRow row;
        bool hasRow = false;
    };

    vector<Reader> readers(partialFiles.size());
    for (size_t i = 0; i < partialFiles.size(); i++)
    {
        readers[i].in.open(partialFiles[i]);
        if (readers[i].in)
            readers[i].hasRow = loadGamePartialRow(readers[i].in, readers[i].row);
    }

    ofstream gout("placement_result_game.csv");
    gout << "seed,game,turn";
    for (int t = 6; t <= 9; t++)
    {
        gout << "," << t << "tuple_straight";
        gout << "," << t << "tuple_2x2";
    }
    gout << "\n";

    while (true)
    {
        int bestIndex = -1;
        tuple<int, int, int> bestKey;

        for (size_t i = 0; i < readers.size(); i++)
        {
            if (!readers[i].hasRow)
                continue;

            if (bestIndex == -1 || readers[i].row.key < bestKey)
            {
                bestIndex = static_cast<int>(i);
                bestKey = readers[i].row.key;
            }
        }

        if (bestIndex == -1)
            break;

        auto& [seed, turn, game] = bestKey;
        gout << seed << "," << game << "," << turn;

        for (int t = 6; t <= 9; t++)
        {
            size_t index = static_cast<size_t>(t - 6);
            if (readers[index].hasRow && readers[index].row.key == bestKey)
            {
                gout << "," << readers[index].row.tail;
                readers[index].hasRow = loadGamePartialRow(readers[index].in, readers[index].row);
            }
            else
            {
                gout << ",,";
            }
        }

        gout << "\n";
    }
}

int extractSeed(const string& path)
{
    auto name = fs::path(path).filename().string();
    auto pos = name.find("_seed");
    if (pos == string::npos)
        return -1;

    pos += 5;
    size_t end = pos;
    while (end < name.size() && name[end] >= '0' && name[end] <= '9')
        end++;

    try
    {
        return stoi(name.substr(pos, end - pos));
    }
    catch (...)
    {
        return -1;
    }
}

array<int, 3> getTop3(const array<int, 16>& board)
{
    int a = -1, b = -1, c = -1;
    int ai = 0, bi = 0, ci = 0;

    for (int i = 0; i < 16; i++)
    {
        int x = board[i];
        if (x > a)
        {
            c = b;
            ci = bi;
            b = a;
            bi = ai;
            a = x;
            ai = i;
        }
        else if (x > b)
        {
            c = b;
            ci = bi;
            b = x;
            bi = i;
        }
        else if (x > c)
        {
            c = x;
            ci = i;
        }
    }

    return {ai, bi, ci};
}

array<bool, 16> getTop3Mask(const array<int, 16>& board)
{
    array<bool, 16> mask{};
    mask.fill(false);
    auto idx = getTop3(board);
    int third = board[idx[2]];
    if (third <= 0)
        return mask;

    for (int i = 0; i < 16; i++)
        if (board[i] >= third && board[i] > 0)
            mask[i] = true;
    return mask;
}

pair<bool, bool> classify(const array<int, 16>& board)
{
    auto p = getTop3(board);

    int r1 = p[0] / 4;
    int c1 = p[0] % 4;
    int r2 = p[1] / 4;
    int c2 = p[1] % 4;
    int r3 = p[2] / 4;
    int c3 = p[2] % 4;

    bool straight = (r1 == r2 && r2 == r3) || (c1 == c2 && c2 == c3);

    int rmin = min({r1, r2, r3});
    int rmax = max({r1, r2, r3});
    int cmin = min({c1, c2, c3});
    int cmax = max({c1, c2, c3});

    bool block2x2 = (rmax - rmin <= 1) && (cmax - cmin <= 1);

    return {straight, block2x2};
}

array<int, 16> normalizeSymmetry(const array<int, 16>& board)
{
    vector<array<int, 16>> cand;

    for (int rot = 0; rot < 4; rot++)
    {
        for (int flip = 0; flip < 2; flip++)
        {
            array<int, 16> b{};

            for (int r = 0; r < 4; r++)
            {
                for (int c = 0; c < 4; c++)
                {
                    int nr = r;
                    int nc = c;

                    for (int k = 0; k < rot; k++)
                    {
                        int tmp = nr;
                        nr = nc;
                        nc = 3 - tmp;
                    }

                    if (flip)
                        nc = 3 - nc;

                    b[nr * 4 + nc] = board[r * 4 + c];
                }
            }

            cand.push_back(b);
        }
    }

    auto score = [](const array<int, 16>& b)
    {
        auto p = getTop3(b);
        vector<pair<int, int>> pos;
        for (int x : p)
            pos.push_back({x / 4, x % 4});
        sort(pos.begin(), pos.end());
        return pos;
    };

    array<int, 16> best = cand[0];
    auto bestScore = score(best);
    for (auto& b : cand)
    {
        auto s = score(b);
        if (s < bestScore)
        {
            best = b;
            bestScore = s;
        }
    }

    return best;
}

void parseFile(
    const string& path,
    int seed,
    int tuple,
    TurnStats* turnStats,
    GameStats* gameStats,
    ofstream* progressOut,
    mutex* progressMutex
)
{
    ifstream fin(path);
    if (!fin)
        return;

    string line;
    int prevGame = -1;
    array<int, 16> prevBoard{};

    while (getline(fin, line))
    {
        if (line.rfind("game,", 0) != 0)
            continue;

        int game;
        int turn;
        if (sscanf(line.c_str(), "game,%d,turn,%d", &game, &turn) != 2)
            continue;

        string boardLine;
        if (!getline(fin, boardLine))
            break;
        if (boardLine.rfind("board,", 0) != 0)
            continue;

        array<int, 16> board{};
        string tmp = boardLine.substr(6);
        int idx = 0;
        size_t pos = 0;

        while (pos < tmp.size() && idx < 16)
        {
            size_t next = tmp.find(",", pos);
            string token;
            if (next == string::npos)
            {
                token = tmp.substr(pos);
                pos = tmp.size();
            }
            else
            {
                token = tmp.substr(pos, next - pos);
                pos = next + 1;
            }

            if (token.find("time") != string::npos)
                break;

            try
            {
                board[idx++] = stoi(token);
            }
            catch (...)
            {
                break;
            }
        }

        if (idx != 16)
            continue;

        if (game == prevGame && board == prevBoard)
            continue;

        prevGame = game;
        prevBoard = board;
        board = normalizeSymmetry(board);

        auto [straight, block] = classify(board);

        if (turnStats != nullptr)
        {
            auto& ts = (*turnStats)[seed][turn][tuple];

            ts.count++;
            ts.straight += straight;
            ts.block2x2 += block;

            auto topMask = getTop3Mask(board);
            for (int i = 0; i < 16; i++)
            {
                if (board[i] > 0)
                {
                    ts.logTile[i] += log2(board[i]);
                    if (topMask[i])
                        ts.top3LogTile[i] += log2(board[i]);
                }
            }
        }

        if (gameStats != nullptr)
        {
            auto& gs = (*gameStats)[makeGameKey(seed, turn, game)][tupleIndex(tuple)];
            gs.count++;
            gs.straight += straight;
            gs.block2x2 += block;
        }

        if (progressOut != nullptr)
        {
            lock_guard<mutex> lock(*progressMutex);
            (*progressOut) << seed << "," << game << "," << turn << "," << tuple << "," << (straight ? 1 : 0) << "," << (block ? 1 : 0) << "\n";
        }
    }
}

static void writeTupleHeader(ofstream& out, const string& prefix)
{
    out << prefix;
    for (int t = 6; t <= 9; t++)
    {
        out << "," << t << "tuple_straight";
        out << "," << t << "tuple_2x2";
    }
    out << "\n";
}

int main()
{
    vector<FileTask> tasks;
    array<vector<string>, 10> filesByTuple;

    for (int t = 6; t <= 9; t++)
    {
        string pattern = to_string(t) + "tuple_seed";
        for (auto& p : fs::directory_iterator("learn_double/logs"))
        {
            string name = p.path().filename().string();
            if (name.find(pattern) != string::npos)
            {
                tasks.push_back({t, p.path().string()});
                filesByTuple[t].push_back(p.path().string());
            }
        }
    }

    const size_t workerCount = 16;

    {
        map<int, map<int, map<int, TurnStat>>> turnStats;
        ofstream tprogress("placement_result_turn_progress.csv");
        tprogress << "seed,game,turn,tuple,straight,block2x2\n";

        mutex ioMutex;
        atomic<size_t> nextTask{0};
        vector<thread> workers;
        workers.reserve(workerCount);

        for (size_t i = 0; i < workerCount; i++)
        {
            workers.emplace_back([&]() {
                while (true)
                {
                    size_t taskIndex = nextTask.fetch_add(1);
                    if (taskIndex >= tasks.size())
                        break;

                    const auto& task = tasks[taskIndex];
                    int seed = extractSeed(task.path);

                    TurnStats localTurnStats;

                    {
                        lock_guard<mutex> lock(ioMutex);
                        cout << "start " << task.path << endl;
                    }

                    parseFile(task.path, seed, task.tuple, &localTurnStats, nullptr, &tprogress, &ioMutex);

                    {
                        lock_guard<mutex> lock(ioMutex);
                        mergeTurnStats(turnStats, localTurnStats);
                        cout << "finish " << task.path << endl;
                    }
                }
            });
        }

        for (auto& worker : workers)
            worker.join();

        ofstream fout("placement_result_turn.csv");
        writeTupleHeader(fout, "seed,turn");
        for (auto& [seed, turnMap] : turnStats)
        {
            for (auto& [turn, m] : turnMap)
            {
                fout << seed << "," << turn;
                for (int t = 6; t <= 9; t++)
                {
                    auto& s = m[t];
                    fout << "," << (double)s.straight / s.count;
                    fout << "," << (double)s.block2x2 / s.count;
                }
                fout << "\n";
            }
        }

        ofstream bout("board_average_log2.csv");
        bout << "seed,turn,tuple";
        for (int i = 0; i < 16; i++)
            bout << ",c" << i;
        bout << ",count\n";
        for (auto& [seed, turnMap] : turnStats)
        {
            for (auto& [turn, m] : turnMap)
            {
                for (auto& [tuple, s] : m)
                {
                    bout << seed << "," << turn << "," << tuple;
                    for (int i = 0; i < 16; i++)
                        bout << "," << s.logTile[i] / s.count;
                    bout << "," << s.count << "\n";
                }
            }
        }

        ofstream bout2("board_average_log2_top3.csv");
        bout2 << "seed,turn,tuple";
        for (int i = 0; i < 16; i++)
            bout2 << ",c" << i;
        bout2 << ",count\n";
        for (auto& [seed, turnMap] : turnStats)
        {
            for (auto& [turn, m] : turnMap)
            {
                for (auto& [tuple, s] : m)
                {
                    bout2 << seed << "," << turn << "," << tuple;
                    for (int i = 0; i < 16; i++)
                        bout2 << "," << s.top3LogTile[i] / s.count;
                    bout2 << "," << s.count << "\n";
                }
            }
        }
    }

    {
        vector<string> partialFiles;

        for (int tuple = 6; tuple <= 9; tuple++)
        {
            const auto& tupleFiles = filesByTuple[tuple];
            if (tupleFiles.empty())
                continue;

            GameStats gameStats;

            mutex ioMutex;
            atomic<size_t> nextTask{0};
            vector<thread> workers;
            size_t tupleWorkerCount = 4;
            if (tupleFiles.size() < tupleWorkerCount)
                tupleWorkerCount = tupleFiles.size();
            workers.reserve(tupleWorkerCount);

            for (size_t i = 0; i < tupleWorkerCount; i++)
            {
                workers.emplace_back([&]() {
                    while (true)
                    {
                        size_t taskIndex = nextTask.fetch_add(1);
                        if (taskIndex >= tupleFiles.size())
                            break;

                        const auto& path = tupleFiles[taskIndex];
                        int seed = extractSeed(path);

                        GameStats localGameStats;

                        {
                            lock_guard<mutex> lock(ioMutex);
                            cout << "start " << path << endl;
                        }

                        parseFile(path, seed, tuple, nullptr, &localGameStats, nullptr, nullptr);

                        {
                            lock_guard<mutex> lock(ioMutex);
                            mergeGameStats(gameStats, localGameStats);
                            cout << "finish " << path << endl;
                        }
                    }
                });
            }

            for (auto& worker : workers)
                worker.join();

            string partialPath = "placement_result_game_tuple" + to_string(tuple) + ".csv";
            ofstream gout(partialPath);
            gout << "seed,game,turn," << tuple << "tuple_straight," << tuple << "tuple_2x2\n";

            for (auto& [key, s] : gameStats)
            {
                auto& [seed, turn, game] = key;
                gout << seed << "," << game << "," << turn;
                if (s.count == 0)
                {
                    gout << ",,";
                }
                else
                {
                    gout << "," << (double)s.straight / s.count;
                    gout << "," << (double)s.block2x2 / s.count;
                }
                gout << "\n";
            }

            partialFiles.push_back(partialPath);
        }

        writeMergedGameCsv(partialFiles);
    }

    cout << "saved" << endl;
}