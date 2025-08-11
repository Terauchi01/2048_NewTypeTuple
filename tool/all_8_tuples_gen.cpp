#include <iostream>
#include <vector>
#include <set>
#include <algorithm>
using namespace std;

// 4x4盤面の座標をインデックスに変換
int idx(int x, int y) { return y * 4 + x; }

int number = 9; // タプルのサイズ
// numberマスのタプルを回転・反転
vector<int> rotate(const vector<int>& t, int rot) {
    vector<pair<int,int>> pos;
    for (int i : t) pos.push_back({i%4, i/4});
    vector<pair<int,int>> newpos(number);
    for (int i = 0; i < number; ++i) {
        int x = pos[i].first, y = pos[i].second;
        if (rot == 0) { newpos[i] = {x, y}; }
        if (rot == 1) { newpos[i] = {3-y, x}; }
        if (rot == 2) { newpos[i] = {3-x, 3-y}; }
        if (rot == 3) { newpos[i] = {y, 3-x}; }
    }
    vector<int> res(number);
    for (int i = 0; i < number; ++i) res[i] = idx(newpos[i].first, newpos[i].second);
    return res;
}
vector<int> reflect(const vector<int>& t, bool vertical) {
    vector<pair<int,int>> pos;
    for (int i : t) pos.push_back({i%4, i/4});
    vector<pair<int,int>> newpos(number);
    for (int i = 0; i < number; ++i) {
        int x = pos[i].first, y = pos[i].second;
        if (vertical) newpos[i] = {3-x, y};
        else newpos[i] = {x, 3-y};
    }
    vector<int> res(number);
    for (int i = 0; i < number; ++i) res[i] = idx(newpos[i].first, newpos[i].second);
    return res;
}

// 代表タプルを取得
vector<int> canonical(const vector<int>& t) {
    vector<vector<int>> candidates;
    for (int r = 0; r < 4; ++r) {
        candidates.push_back(rotate(t, r));
        candidates.push_back(reflect(rotate(t, r), true));
        candidates.push_back(reflect(rotate(t, r), false));
    }
    for (auto& v : candidates) std::sort(v.begin(), v.end());
    return *min_element(candidates.begin(), candidates.end());
}

int main() {

// 隣接判定
auto pos = [](int i) { return make_pair(i%4, i/4); };
auto is_connected = [&](const vector<int>& cells) {
    set<int> cellset(cells.begin(), cells.end());
    queue<int> q;
    set<int> visited;
    q.push(cells[0]);
    visited.insert(cells[0]);
    while (!q.empty()) {
        int cur = q.front(); q.pop();
        auto [x, y] = pos(cur);
        for (auto [dx, dy] : vector<pair<int,int>>{{1,0},{-1,0},{0,1},{0,-1}}) {
            int nx = x+dx, ny = y+dy;
            if (nx<0||nx>=4||ny<0||ny>=4) continue;
            int ni = idx(nx, ny);
            if (cellset.count(ni) && !visited.count(ni)) {
                visited.insert(ni);
                q.push(ni);
            }
        }
    }
    return visited.size() == number;
};

set<vector<int>> unique_tuples;
vector<int> board(16);
for (int i = 0; i < 16; ++i) board[i] = i;

// 16マスからnumberマスの全組み合わせ
vector<int> comb(16, 0);
fill(comb.begin(), comb.begin()+number, 1);
do {
    vector<int> t;
    for (int i = 0; i < 16; ++i) if (comb[i]) t.push_back(board[i]);
    if (is_connected(t)) {
        unique_tuples.insert(canonical(t));
    }
} while (prev_permutation(comb.begin(), comb.end()));

// 出力
cout << "#define AVAIL_TUPLE " << unique_tuples.size() << endl;
cout << "int tuples[AVAIL_TUPLE][number] = {" << endl;
int k = number-1;
for (const auto& t : unique_tuples) {
    cout << "  {";
    for (int i = 0; i < number; ++i) cout << t[i] << (i<8?", ":"");
    cout << "}," << endl;
}
cout << "};" << endl;
return 0;
}