#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

using namespace std;

namespace {

constexpr int SIDE = 4;
constexpr int CELLS = 16;
constexpr int FREE_CELLS = 8;
constexpr uint32_t NO_PARENT = numeric_limits<uint32_t>::max();
constexpr array<char, 4> DIR_NAMES{'U', 'R', 'D', 'L'};

struct Tile {
    uint8_t exponent = 0;
    bool protected_tile = false;
};

using Board = array<Tile, CELLS>;

struct Config {
    string layout = "straight";
    string mode = "witness";
    int target_exponent = 15;
    int protected_max_exponent = 15;
    int initial_spawns = 2;
    uint64_t max_states = 10'000'000;
    double max_seconds = 0.0;
    uint64_t progress_interval = 1'000'000;
    double spawn_four_probability = 0.1;
    string witness_output;
    vector<int> rank_cells_override;
    bool self_test = false;
};

struct Layout {
    string name;
    array<int, FREE_CELLS> rank_cells;
    array<int, FREE_CELLS> free_cells;
    Board protected_board{};
};

struct Node {
    uint32_t code = 0;
    uint32_t parent = NO_PARENT;
    uint32_t moves = 0;
    uint32_t spawns = 0;
    uint8_t move = 0xff;
    uint8_t spawn_slot = 0xff;
    uint8_t spawn_exponent = 0;
    uint16_t initial_spawn_events = 0;
};

struct Goal {
    bool found = false;
    uint32_t parent = NO_PARENT;
    uint32_t moves = 0;
    uint32_t spawns = 0;
    uint8_t final_move = 0xff;
};

struct MoveResult {
    bool legal = false;
    Board board{};
};

struct Enumeration {
    vector<Node> nodes;
    unordered_map<uint32_t, uint32_t> index;
    Goal goal;
    bool complete = true;
    uint64_t dead_states = 0;
    array<uint32_t, 16> first_milestone_moves{};
    array<uint32_t, 16> first_milestone_spawns{};
    double elapsed_seconds = 0.0;
};

[[noreturn]] void fail(const string& message) {
    throw runtime_error(message);
}

uint64_t parse_u64(const string& text, const string& option) {
    size_t end = 0;
    const auto value = stoull(text, &end);
    if (end != text.size()) fail("invalid value for " + option + ": " + text);
    return value;
}

int parse_int(const string& text, const string& option) {
    size_t end = 0;
    const auto value = stoi(text, &end);
    if (end != text.size()) fail("invalid value for " + option + ": " + text);
    return value;
}

double parse_double(const string& text, const string& option) {
    size_t end = 0;
    const auto value = stod(text, &end);
    if (end != text.size()) fail("invalid value for " + option + ": " + text);
    return value;
}

void usage(ostream& out) {
    out << "Usage: exact_second_32768 [options]\n"
        << "  --layout straight|block2x2\n"
        << "  --rank-cells C0,...,C7   override rank-1..8 cells\n"
        << "  --target-exponent N       15 means 32768 (default: 15)\n"
        << "  --protected-max-exponent N  rank-1 protected value exponent (default: 15)\n"
        << "  --initial-spawns N        initial random tiles (default: 2)\n"
        << "  --mode witness|enumerate|guarantee|probability\n"
        << "  --spawn-four-probability P  probability of a 4 spawn (default: 0.1)\n"
        << "  --max-states N            safety limit (default: 10000000)\n"
        << "  --max-seconds S           0 disables the time limit\n"
        << "  --progress-interval N     0 disables progress output\n"
        << "  --witness-output PATH     write a CSV witness when found\n"
        << "  --self-test\n";
}

Config parse_args(int argc, char** argv) {
    Config config;
    for (int i = 1; i < argc; ++i) {
        const string arg = argv[i];
        auto value = [&](const string& option) -> string {
            if (++i >= argc) fail("missing value for " + option);
            return argv[i];
        };
        if (arg == "--layout") config.layout = value(arg);
        else if (arg == "--rank-cells") {
            const string cells = value(arg);
            size_t begin = 0;
            while (begin <= cells.size()) {
                const size_t end = cells.find(',', begin);
                config.rank_cells_override.push_back(parse_int(
                    cells.substr(begin, end - begin), arg));
                if (end == string::npos) break;
                begin = end + 1;
            }
        }
        else if (arg == "--target-exponent") config.target_exponent = parse_int(value(arg), arg);
        else if (arg == "--protected-max-exponent")
            config.protected_max_exponent = parse_int(value(arg), arg);
        else if (arg == "--initial-spawns") config.initial_spawns = parse_int(value(arg), arg);
        else if (arg == "--mode") config.mode = value(arg);
        else if (arg == "--max-states") config.max_states = parse_u64(value(arg), arg);
        else if (arg == "--max-seconds") config.max_seconds = parse_double(value(arg), arg);
        else if (arg == "--progress-interval") config.progress_interval = parse_u64(value(arg), arg);
        else if (arg == "--spawn-four-probability")
            config.spawn_four_probability = parse_double(value(arg), arg);
        else if (arg == "--witness-output") config.witness_output = value(arg);
        else if (arg == "--self-test") config.self_test = true;
        else if (arg == "--help" || arg == "-h") { usage(cout); exit(0); }
        else fail("unknown option: " + arg);
    }
    if (config.layout != "straight" && config.layout != "block2x2")
        fail("layout must be straight or block2x2");
    if (!config.rank_cells_override.empty()) {
        if (config.rank_cells_override.size() != FREE_CELLS)
            fail("rank-cells must contain exactly eight comma-separated cells");
        array<bool, CELLS> used{};
        for (int cell : config.rank_cells_override) {
            if (cell < 0 || cell >= CELLS || used[cell])
                fail("rank-cells must be eight distinct values in 0..15");
            used[cell] = true;
        }
    }
    if (config.mode != "witness" && config.mode != "enumerate" &&
        config.mode != "guarantee" && config.mode != "probability")
        fail("mode must be witness, enumerate, guarantee, or probability");
    if (config.target_exponent < 1 || config.target_exponent > 15)
        fail("target exponent must be in 1..15");
    if (config.protected_max_exponent < 8 || config.protected_max_exponent > 15)
        fail("protected max exponent must be in 8..15");
    if (config.initial_spawns != 2)
        fail("the current exact model requires exactly two initial spawns");
    if (config.max_states == 0) fail("max-states must be positive");
    if (!isfinite(config.spawn_four_probability) ||
        config.spawn_four_probability < 0.0 || config.spawn_four_probability > 1.0)
        fail("spawn-four-probability must be in 0..1");
    return config;
}

Layout make_layout(const Config& config) {
    Layout layout;
    layout.name = config.rank_cells_override.empty() ? config.layout : "custom";
    if (!config.rank_cells_override.empty()) {
        copy(config.rank_cells_override.begin(), config.rank_cells_override.end(),
             layout.rank_cells.begin());
    } else if (config.layout == "straight") {
        // Maximum-weight assignment for top-1..8 in the straight samples.
        layout.rank_cells = {0, 1, 2, 3, 7, 6, 5, 4};
    } else {
        // Maximum-weight assignment for top-1..8 in the block-2x2 samples.
        layout.rank_cells = {0, 4, 1, 5, 2, 9, 6, 7};
    }

    array<bool, CELLS> occupied{};
    for (int rank = 0; rank < FREE_CELLS; ++rank) {
        const int cell = layout.rank_cells[rank];
        occupied[cell] = true;
        layout.protected_board[cell] = Tile{
            static_cast<uint8_t>(config.protected_max_exponent - rank), true};
    }
    int slot = 0;
    for (int cell = 0; cell < CELLS; ++cell)
        if (!occupied[cell]) layout.free_cells[slot++] = cell;
    if (slot != FREE_CELLS) fail("layout does not contain exactly eight protected cells");
    return layout;
}

uint32_t encode(const Board& board, const Layout& layout) {
    uint32_t code = 0;
    for (int slot = FREE_CELLS - 1; slot >= 0; --slot) {
        const auto tile = board[layout.free_cells[slot]];
        if (tile.protected_tile) fail("protected tile entered a free cell");
        if (tile.exponent > 15) fail("tile exponent exceeds four-bit encoding");
        code = (code << 4) | tile.exponent;
    }
    return code;
}

Board decode(uint32_t code, const Layout& layout) {
    Board board = layout.protected_board;
    for (int slot = 0; slot < FREE_CELLS; ++slot) {
        board[layout.free_cells[slot]].exponent = code & 0x0f;
        code >>= 4;
    }
    return board;
}

array<array<int, SIDE>, SIDE> line_coordinates(int direction) {
    array<array<int, SIDE>, SIDE> lines{};
    for (int outer = 0; outer < SIDE; ++outer) {
        for (int inner = 0; inner < SIDE; ++inner) {
            int row = 0, column = 0;
            if (direction == 0) { row = inner; column = outer; }          // up
            if (direction == 1) { row = outer; column = SIDE - 1 - inner; } // right
            if (direction == 2) { row = SIDE - 1 - inner; column = outer; } // down
            if (direction == 3) { row = outer; column = inner; }          // left
            lines[outer][inner] = row * SIDE + column;
        }
    }
    return lines;
}

MoveResult apply_move(const Board& board, const Layout& layout, int direction) {
    Board result{};
    const auto lines = line_coordinates(direction);
    for (const auto& line : lines) {
        array<Tile, SIDE> compact{};
        int count = 0;
        for (int cell : line)
            if (board[cell].exponent) compact[count++] = board[cell];

        array<Tile, SIDE> merged{};
        int output = 0;
        for (int i = 0; i < count;) {
            if (i + 1 < count && compact[i].exponent == compact[i + 1].exponent) {
                merged[output++] = Tile{
                    static_cast<uint8_t>(compact[i].exponent + 1),
                    compact[i].protected_tile || compact[i + 1].protected_tile,
                };
                i += 2;
            } else {
                merged[output++] = compact[i++];
            }
        }
        for (int i = 0; i < output; ++i) result[line[i]] = merged[i];
    }

    bool changed = false;
    for (int cell = 0; cell < CELLS; ++cell) {
        if (result[cell].exponent != board[cell].exponent ||
            result[cell].protected_tile != board[cell].protected_tile)
            changed = true;
        const Tile expected = layout.protected_board[cell];
        if (expected.protected_tile) {
            if (!result[cell].protected_tile || result[cell].exponent != expected.exponent)
                return {};
        } else if (result[cell].protected_tile) {
            return {};
        }
    }
    if (!changed) return {};
    return {true, result};
}

int max_free_exponent(const Board& board) {
    int result = 0;
    for (const auto tile : board)
        if (!tile.protected_tile) result = max(result, static_cast<int>(tile.exponent));
    return result;
}

vector<pair<uint32_t, pair<uint8_t, uint8_t>>> spawn_successors(
    const Board& after_move, const Layout& layout) {
    vector<pair<uint32_t, pair<uint8_t, uint8_t>>> successors;
    for (int slot = 0; slot < FREE_CELLS; ++slot) {
        const int cell = layout.free_cells[slot];
        if (after_move[cell].exponent) continue;
        for (uint8_t exponent : {uint8_t{1}, uint8_t{2}}) {
            Board spawned = after_move;
            spawned[cell].exponent = exponent;
            successors.push_back({encode(spawned, layout), {static_cast<uint8_t>(slot), exponent}});
        }
    }
    return successors;
}

bool time_exceeded(const Config& config, chrono::steady_clock::time_point start) {
    if (config.max_seconds <= 0.0) return false;
    return chrono::duration<double>(chrono::steady_clock::now() - start).count() >= config.max_seconds;
}

void note_milestone(Enumeration& result, int exponent, uint32_t moves, uint32_t spawns) {
    for (int value = 1; value <= exponent && value < 16; ++value) {
        if (result.first_milestone_moves[value] == numeric_limits<uint32_t>::max()) {
            result.first_milestone_moves[value] = moves;
            result.first_milestone_spawns[value] = spawns;
        }
    }
}

Enumeration enumerate(const Config& config, const Layout& layout) {
    Enumeration result;
    result.first_milestone_moves.fill(numeric_limits<uint32_t>::max());
    result.first_milestone_spawns.fill(numeric_limits<uint32_t>::max());
    result.nodes.reserve(static_cast<size_t>(min<uint64_t>(config.max_states, 20'000'000)));
    result.index.reserve(static_cast<size_t>(min<uint64_t>(config.max_states, 20'000'000)));
    const auto start = chrono::steady_clock::now();

    // Standard 2048 has two random tiles on the board before the first move.
    const Board empty = layout.protected_board;
    for (const auto& first : spawn_successors(empty, layout)) {
        const Board once = decode(first.first, layout);
        for (const auto& second : spawn_successors(once, layout)) {
            const uint32_t code = second.first;
            if (result.index.find(code) != result.index.end()) continue;
            if (result.nodes.size() >= config.max_states) {
                result.complete = false;
                break;
            }
            const uint8_t first_event = static_cast<uint8_t>(
                first.second.first | ((first.second.second - 1) << 3));
            const uint8_t second_event = static_cast<uint8_t>(
                second.second.first | ((second.second.second - 1) << 3));
            Node node{code, NO_PARENT, 0, 2, 0xff, 0xff, 0,
                      static_cast<uint16_t>(first_event | (second_event << 4))};
            const uint32_t index = static_cast<uint32_t>(result.nodes.size());
            result.nodes.push_back(node);
            result.index.emplace(code, index);
            note_milestone(result, max(first.second.second, second.second.second), 0, 2);
            if (max(first.second.second, second.second.second) >= config.target_exponent &&
                !result.goal.found)
                result.goal = {true, index, 0, 2, 0xff};
        }
        if (!result.complete) break;
    }

    for (uint64_t head = 0; head < result.nodes.size(); ++head) {
        if (result.nodes.size() >= config.max_states || time_exceeded(config, start)) {
            result.complete = false;
            break;
        }
        if (config.progress_interval && head && head % config.progress_interval == 0) {
            const double seconds = chrono::duration<double>(chrono::steady_clock::now() - start).count();
            cerr << "expanded=" << head << " discovered=" << result.nodes.size()
                 << " states_per_second=" << fixed << setprecision(0) << head / max(seconds, 1e-9) << '\n';
        }

        // Copy because vector growth can invalidate references.
        const Node node = result.nodes[head];
        const Board board = decode(node.code, layout);
        bool has_legal_move = false;
        for (int direction = 0; direction < 4; ++direction) {
            const auto moved = apply_move(board, layout, direction);
            if (!moved.legal) continue;
            has_legal_move = true;
            const uint32_t next_moves = node.moves + 1;
            const int maximum = max_free_exponent(moved.board);
            note_milestone(result, maximum, next_moves, node.spawns);
            if (maximum >= config.target_exponent) {
                if (!result.goal.found) {
                    result.goal = {true, static_cast<uint32_t>(head), next_moves,
                                   node.spawns, static_cast<uint8_t>(direction)};
                }
                continue;
            }

            for (const auto& successor : spawn_successors(moved.board, layout)) {
                const auto [code, spawn] = successor;
                if (result.index.find(code) != result.index.end()) continue;
                if (result.nodes.size() >= config.max_states) {
                    result.complete = false;
                    break;
                }
                const uint32_t index = static_cast<uint32_t>(result.nodes.size());
                Node child{code, static_cast<uint32_t>(head), next_moves,
                           node.spawns + 1, static_cast<uint8_t>(direction),
                           spawn.first, spawn.second, 0};
                result.nodes.push_back(child);
                result.index.emplace(code, index);
                note_milestone(result, max(maximum, static_cast<int>(spawn.second)),
                               next_moves, node.spawns + 1);
            }
            if (!result.complete) break;
        }
        if (!has_legal_move) ++result.dead_states;
        if (!result.complete) break;
        if (config.mode == "witness" && result.goal.found) {
            result.complete = false; // existence is proven; the state graph is partial
            break;
        }
    }
    result.elapsed_seconds = chrono::duration<double>(chrono::steady_clock::now() - start).count();
    return result;
}

struct WitnessStep {
    uint32_t step = 0;
    char move = '-';
    int spawn_cell = -1;
    int spawn_value = 0;
    uint32_t code = 0;
};

vector<WitnessStep> build_witness(const Enumeration& result, const Layout& layout) {
    if (!result.goal.found) return {};
    vector<uint32_t> path;
    uint32_t index = result.goal.parent;
    while (index != NO_PARENT) {
        path.push_back(index);
        index = result.nodes[index].parent;
    }
    reverse(path.begin(), path.end());
    vector<WitnessStep> steps;
    uint32_t number = 0;
    for (uint32_t node_index : path) {
        const Node& node = result.nodes[node_index];
        if (node.parent == NO_PARENT) {
            for (int event_index = 0; event_index < 2; ++event_index) {
                const uint8_t event = (node.initial_spawn_events >> (4 * event_index)) & 0x0f;
                const uint8_t slot = event & 0x07;
                const uint8_t exponent = 1 + ((event >> 3) & 0x01);
                steps.push_back({number++, '-', layout.free_cells[slot],
                                 1 << exponent, event_index == 1 ? node.code : 0});
            }
        } else {
            steps.push_back({number++, DIR_NAMES[node.move],
                             layout.free_cells[node.spawn_slot], 1 << node.spawn_exponent,
                             node.code});
        }
    }
    if (result.goal.final_move != 0xff)
        steps.push_back({number, DIR_NAMES[result.goal.final_move], -1, 0, 0});
    return steps;
}

void verify_goal_witness(const Enumeration& result, const Layout& layout, int target) {
    if (!result.goal.found) return;
    vector<uint32_t> path;
    for (uint32_t index = result.goal.parent; index != NO_PARENT;
         index = result.nodes[index].parent)
        path.push_back(index);
    reverse(path.begin(), path.end());
    if (path.empty()) fail("goal witness has no root state");

    Board board = layout.protected_board;
    const Node& root = result.nodes[path.front()];
    for (int event_index = 0; event_index < 2; ++event_index) {
        const uint8_t event = (root.initial_spawn_events >> (4 * event_index)) & 0x0f;
        const uint8_t slot = event & 0x07;
        const uint8_t exponent = 1 + ((event >> 3) & 0x01);
        const int cell = layout.free_cells[slot];
        if (board[cell].exponent) fail("witness initial spawn uses an occupied cell");
        board[cell].exponent = exponent;
    }
    if (encode(board, layout) != root.code) fail("witness root state mismatch");

    for (size_t position = 1; position < path.size(); ++position) {
        const Node& node = result.nodes[path[position]];
        const auto moved = apply_move(board, layout, node.move);
        if (!moved.legal) fail("witness contains an illegal move");
        board = moved.board;
        const int cell = layout.free_cells[node.spawn_slot];
        if (board[cell].exponent) fail("witness spawn uses an occupied cell");
        board[cell].exponent = node.spawn_exponent;
        if (encode(board, layout) != node.code) fail("witness state code mismatch");
    }

    if (result.goal.final_move == 0xff) {
        if (max_free_exponent(board) < target) fail("witness root does not meet target");
    } else {
        const auto moved = apply_move(board, layout, result.goal.final_move);
        if (!moved.legal || max_free_exponent(moved.board) < target)
            fail("witness final move does not meet target");
    }
}

void write_witness(const string& path, const vector<WitnessStep>& witness) {
    ofstream output(path);
    if (!output) fail("cannot write witness: " + path);
    output << "step,move,spawn_cell,spawn_value,state_code\n";
    for (const auto& step : witness)
        output << step.step << ',' << step.move << ',' << step.spawn_cell << ','
               << step.spawn_value << ',' << step.code << '\n';
}

bool action_forces_win(const Board& board, const Layout& layout, int direction,
                       int target, const Enumeration& enumeration,
                       const vector<uint8_t>& winning) {
    const auto moved = apply_move(board, layout, direction);
    if (!moved.legal) return false;
    if (max_free_exponent(moved.board) >= target) return true;
    const auto successors = spawn_successors(moved.board, layout);
    if (successors.empty()) return false;
    for (const auto& successor : successors) {
        const auto found = enumeration.index.find(successor.first);
        if (found == enumeration.index.end() || !winning[found->second]) return false;
    }
    return true;
}

pair<bool, uint64_t> analyze_guarantee(const Config& config, const Layout& layout,
                                      const Enumeration& enumeration) {
    if (!enumeration.complete) fail("guarantee analysis requires complete enumeration");
    vector<uint8_t> winning(enumeration.nodes.size(), 0);
    uint64_t total_winning = 0;
    for (uint32_t index = 0; index < enumeration.nodes.size(); ++index) {
        if (max_free_exponent(decode(enumeration.nodes[index].code, layout)) >=
            config.target_exponent) {
            winning[index] = 1;
            ++total_winning;
        }
    }
    bool changed = true;
    uint64_t passes = 0;
    while (changed) {
        changed = false;
        ++passes;
        for (uint32_t index = 0; index < enumeration.nodes.size(); ++index) {
            if (winning[index]) continue;
            const Board board = decode(enumeration.nodes[index].code, layout);
            for (int direction = 0; direction < 4; ++direction) {
                if (action_forces_win(board, layout, direction, config.target_exponent,
                                      enumeration, winning)) {
                    winning[index] = 1;
                    ++total_winning;
                    changed = true;
                    break;
                }
            }
        }
        cerr << "guarantee_pass=" << passes << " winning=" << total_winning << '\n';
    }

    const Board empty = layout.protected_board;
    bool initial_guaranteed = true;
    for (const auto& first : spawn_successors(empty, layout)) {
        const Board once = decode(first.first, layout);
        for (const auto& second : spawn_successors(once, layout)) {
            const auto found = enumeration.index.find(second.first);
            if (found == enumeration.index.end() || !winning[found->second]) {
                initial_guaranteed = false;
                break;
            }
        }
        if (!initial_guaranteed) break;
    }
    return {initial_guaranteed, total_winning};
}

struct ProbabilityResult {
    double initial_probability = 0.0;
    uint64_t initial_lower_scaled = 0;
    uint64_t initial_upper_scaled = 0;
    uint64_t positive_states = 0;
    uint64_t certain_states = 0;
    double elapsed_seconds = 0.0;
};

constexpr uint64_t PROBABILITY_SCALE = 1'000'000'000'000'000ULL;
__extension__ using uint128_t = unsigned __int128;

uint64_t floor_ratio(uint128_t numerator, uint64_t denominator) {
    return static_cast<uint64_t>(numerator / denominator);
}

uint64_t ceil_ratio(uint128_t numerator, uint64_t denominator) {
    return static_cast<uint64_t>((numerator + denominator - 1) / denominator);
}

uint32_t free_tile_sum(uint32_t code) {
    uint32_t sum = 0;
    for (int slot = 0; slot < FREE_CELLS; ++slot) {
        const uint8_t exponent = code & 0x0f;
        if (exponent) sum += 1u << exponent;
        code >>= 4;
    }
    return sum;
}

ProbabilityResult analyze_probability(const Config& config, const Layout& layout,
                                      const Enumeration& enumeration) {
    if (!enumeration.complete)
        fail("probability analysis requires complete enumeration");
    const auto start = chrono::steady_clock::now();
    const size_t state_count = enumeration.nodes.size();

    // Every nonterminal edge consists of a mass-preserving move followed by a
    // 2/4 spawn.  Free-tile mass therefore increases strictly along every edge.
    // Counting-sort states by mass and evaluate them in descending order; this
    // is an exact finite dynamic program, not iterative value approximation.
    vector<uint32_t> masses(state_count);
    uint32_t maximum_mass = 0;
    for (size_t index = 0; index < state_count; ++index) {
        masses[index] = free_tile_sum(enumeration.nodes[index].code);
        maximum_mass = max(maximum_mass, masses[index]);
    }
    vector<uint32_t> counts(static_cast<size_t>(maximum_mass) + 1, 0);
    for (uint32_t mass : masses) ++counts[mass];
    vector<uint32_t> offsets(counts.size(), 0);
    uint32_t running = 0;
    for (size_t mass = 0; mass < counts.size(); ++mass) {
        offsets[mass] = running;
        running += counts[mass];
    }
    vector<uint32_t> cursors = offsets;
    vector<uint32_t> order(state_count);
    for (uint32_t index = 0; index < state_count; ++index)
        order[cursors[masses[index]]++] = index;

    const double probability_four = config.spawn_four_probability;
    const double probability_two = 1.0 - probability_four;
    // Certified bounds currently use the standard 90%/10% spawn distribution.
    if (abs(probability_four - 0.1) > 1e-15)
        fail("certified probability mode currently requires spawn-four-probability 0.1");
    vector<double> values(state_count, 0.0);
    vector<uint64_t> lower(state_count, 0);
    vector<uint64_t> upper(state_count, 0);
    uint64_t processed = 0;
    for (auto position = order.rbegin(); position != order.rend(); ++position) {
        const uint32_t index = *position;
        const Board board = decode(enumeration.nodes[index].code, layout);
        if (max_free_exponent(board) >= config.target_exponent) {
            values[index] = 1.0;
            lower[index] = PROBABILITY_SCALE;
            upper[index] = PROBABILITY_SCALE;
            continue;
        }

        double best = 0.0;
        uint64_t best_lower = 0;
        uint64_t best_upper = 0;
        for (int direction = 0; direction < 4; ++direction) {
            const MoveResult moved = apply_move(board, layout, direction);
            if (!moved.legal) continue;
            if (max_free_exponent(moved.board) >= config.target_exponent) {
                best = 1.0;
                best_lower = PROBABILITY_SCALE;
                best_upper = PROBABILITY_SCALE;
                break;
            }
            const auto successors = spawn_successors(moved.board, layout);
            if (successors.empty()) continue;
            const size_t empty_cells = successors.size() / 2;
            double action_value = 0.0;
            uint128_t action_lower_numerator = 0;
            uint128_t action_upper_numerator = 0;
            for (const auto& successor : successors) {
                const auto found = enumeration.index.find(successor.first);
                if (found == enumeration.index.end())
                    fail("complete probability graph is missing a spawn successor");
                if (masses[found->second] <= masses[index])
                    fail("probability graph is not strictly increasing in free-tile mass");
                const double tile_probability = successor.second.second == 2
                    ? probability_four : probability_two;
                action_value += tile_probability * values[found->second] /
                                static_cast<double>(empty_cells);
                const uint64_t integer_weight = successor.second.second == 2 ? 1 : 9;
                action_lower_numerator +=
                    static_cast<uint128_t>(integer_weight) * lower[found->second];
                action_upper_numerator +=
                    static_cast<uint128_t>(integer_weight) * upper[found->second];
            }
            best = max(best, action_value);
            const uint64_t denominator = 10 * empty_cells;
            best_lower = max(best_lower,
                             floor_ratio(action_lower_numerator, denominator));
            best_upper = max(best_upper,
                             ceil_ratio(action_upper_numerator, denominator));
        }
        values[index] = best;
        lower[index] = best_lower;
        upper[index] = best_upper;
        if (lower[index] > upper[index] || upper[index] > PROBABILITY_SCALE)
            fail("invalid certified probability interval");
        ++processed;
        if (config.progress_interval && processed % config.progress_interval == 0)
            cerr << "probability_processed=" << processed << '/' << state_count << '\n';
    }

    ProbabilityResult result;
    for (size_t index = 0; index < state_count; ++index) {
        if (upper[index] > 0) ++result.positive_states;
        if (lower[index] == PROBABILITY_SCALE) ++result.certain_states;
    }

    // The two initial tiles are ordered spawn events.  Positions are uniform
    // among currently empty cells and values use the configured 2/4 weights.
    const Board empty = layout.protected_board;
    const auto first_successors = spawn_successors(empty, layout);
    const size_t first_empty_cells = first_successors.size() / 2;
    uint128_t initial_lower_numerator = 0;
    uint128_t initial_upper_numerator = 0;
    for (const auto& first : first_successors) {
        const double first_tile_probability = first.second.second == 2
            ? probability_four : probability_two;
        const Board once = decode(first.first, layout);
        const auto second_successors = spawn_successors(once, layout);
        const size_t second_empty_cells = second_successors.size() / 2;
        for (const auto& second : second_successors) {
            const auto found = enumeration.index.find(second.first);
            if (found == enumeration.index.end())
                fail("complete probability graph is missing an initial state");
            const double second_tile_probability = second.second.second == 2
                ? probability_four : probability_two;
            result.initial_probability +=
                (first_tile_probability / static_cast<double>(first_empty_cells)) *
                (second_tile_probability / static_cast<double>(second_empty_cells)) *
                values[found->second];
            const uint64_t first_weight = first.second.second == 2 ? 1 : 9;
            const uint64_t second_weight = second.second.second == 2 ? 1 : 9;
            initial_lower_numerator += static_cast<uint128_t>(first_weight) *
                                       second_weight * lower[found->second];
            initial_upper_numerator += static_cast<uint128_t>(first_weight) *
                                       second_weight * upper[found->second];
        }
    }
    const uint64_t initial_denominator =
        100 * first_empty_cells * (first_empty_cells - 1);
    result.initial_lower_scaled =
        floor_ratio(initial_lower_numerator, initial_denominator);
    result.initial_upper_scaled =
        ceil_ratio(initial_upper_numerator, initial_denominator);
    if (result.initial_lower_scaled > result.initial_upper_scaled ||
        result.initial_upper_scaled > PROBABILITY_SCALE)
        fail("invalid certified initial probability interval");
    result.elapsed_seconds =
        chrono::duration<double>(chrono::steady_clock::now() - start).count();
    return result;
}

void require(bool condition, const string& message) {
    if (!condition) fail("self-test failed: " + message);
}

void self_test() {
    Config straight_config;
    straight_config.layout = "straight";
    const Layout straight = make_layout(straight_config);
    Board board = straight.protected_board;
    // Bottom row: 2 2 2 2 -> left -> 4 4 . .
    for (int cell : {12, 13, 14, 15}) board[cell].exponent = 1;
    const auto left = apply_move(board, straight, 3);
    require(left.legal, "free bottom row should move left");
    require(left.board[12].exponent == 2 && left.board[13].exponent == 2 &&
            left.board[14].exponent == 0 && left.board[15].exponent == 0,
            "standard one-merge-per-tile rule");

    // Down would move the protected top two rows and must be rejected.
    require(!apply_move(board, straight, 2).legal, "protected movement must be rejected");
    Board protected_merge = straight.protected_board;
    protected_merge[8].exponent = 8;
    require(!apply_move(protected_merge, straight, 0).legal,
            "protected/free merge must be rejected");
    const uint32_t code = encode(board, straight);
    require(encode(decode(code, straight), straight) == code, "encoding round trip");

    Config tiny;
    tiny.layout = "straight";
    tiny.target_exponent = 2;
    tiny.max_states = 1000;
    tiny.progress_interval = 0;
    const Enumeration enumeration = enumerate(tiny, straight);
    require(enumeration.goal.found, "a spawned 4 should meet target exponent 2");
    require(enumeration.goal.spawns == 2, "target 4 should use the two initial spawns");

    Config probability_test = tiny;
    probability_test.target_exponent = 1;
    probability_test.mode = "probability";
    const Enumeration probability_enumeration = enumerate(probability_test, straight);
    const ProbabilityResult probability = analyze_probability(
        probability_test, straight, probability_enumeration);
    require(abs(probability.initial_probability - 1.0) < 1e-12,
            "an initial 2-or-4 spawn must reach target 2 with probability one");
    require(probability.initial_lower_scaled == PROBABILITY_SCALE &&
            probability.initial_upper_scaled == PROBABILITY_SCALE,
            "target 2 probability interval must be exactly one");
    cout << "self-test: OK\n";
}

void print_summary(const Config& config, const Layout& layout,
                   const Enumeration& result) {
    int maximum_reachable = 0;
    for (int exponent = 1; exponent < 16; ++exponent)
        if (result.first_milestone_moves[exponent] != numeric_limits<uint32_t>::max())
            maximum_reachable = exponent;
    cout << "layout=" << layout.name << '\n'
         << "protected_rank_cells=";
    for (int rank = 0; rank < FREE_CELLS; ++rank)
        cout << (rank ? "," : "") << layout.rank_cells[rank];
    cout << '\n'
         << "protected_max_exponent=" << config.protected_max_exponent << '\n'
         << "protected_values=";
    for (int rank = 0; rank < FREE_CELLS; ++rank)
        cout << (rank ? "," : "") << (1u << (config.protected_max_exponent - rank));
    cout << '\n'
         << "protected_move_budget=0\n"
         << "initial_spawns=" << config.initial_spawns << '\n'
         << "target_exponent=" << config.target_exponent << '\n'
         << "target_tile=" << (1u << config.target_exponent) << '\n'
         << "mode=" << config.mode << '\n'
         << "states=" << result.nodes.size() << '\n'
         << "complete=" << (result.complete ? "true" : "false") << '\n'
         << "elapsed_seconds=" << fixed << setprecision(6) << result.elapsed_seconds << '\n'
         << "states_per_second=" << fixed << setprecision(2)
         << result.nodes.size() / max(result.elapsed_seconds, 1e-9) << '\n'
         << "dead_states=" << result.dead_states << '\n';
    if (result.complete)
        cout << "maximum_reachable_tile=" << (maximum_reachable ? (1u << maximum_reachable) : 0) << '\n';
    else
        cout << "maximum_reachable_tile_lower_bound="
             << (maximum_reachable ? (1u << maximum_reachable) : 0) << '\n';
    cout << "possible="
         << (result.goal.found ? "true" : (result.complete ? "false" : "unknown")) << '\n';
    if (result.goal.found) {
        cout << "minimum_moves=" << result.goal.moves << '\n'
             << "minimum_spawns=" << result.goal.spawns << '\n';
    }
    cout << "milestones:\n";
    for (int exponent = 1; exponent <= config.target_exponent; ++exponent) {
        if (result.first_milestone_moves[exponent] == numeric_limits<uint32_t>::max()) break;
        cout << "  tile=" << (1u << exponent)
             << " moves=" << result.first_milestone_moves[exponent]
             << " spawns=" << result.first_milestone_spawns[exponent] << '\n';
    }
}

} // namespace

int main(int argc, char** argv) {
    try {
        const Config config = parse_args(argc, argv);
        if (config.self_test) {
            self_test();
            return 0;
        }
        const Layout layout = make_layout(config);
        Enumeration result = enumerate(config, layout);
        print_summary(config, layout, result);
        if (result.goal.found) {
            verify_goal_witness(result, layout, config.target_exponent);
            const auto witness = build_witness(result, layout);
            if (!config.witness_output.empty()) {
                write_witness(config.witness_output, witness);
                cout << "witness_output=" << config.witness_output << '\n';
            }
        }
        if (config.mode == "guarantee") {
            const auto [guaranteed, winning_states] = analyze_guarantee(
                config, layout, result);
            cout << "guaranteed=" << (guaranteed ? "true" : "false") << '\n'
                 << "guaranteed_winning_states=" << winning_states << '\n';
        } else if (config.mode == "probability") {
            const ProbabilityResult probability = analyze_probability(
                config, layout, result);
            cout << "spawn_position_distribution=uniform_empty_cell\n"
                 << "spawn_two_probability=" << fixed << setprecision(12)
                 << 1.0 - config.spawn_four_probability << '\n'
                 << "spawn_four_probability=" << fixed << setprecision(12)
                 << config.spawn_four_probability << '\n'
                 << "probability_policy=optimal_player\n"
                 << "probability_method=acyclic_mass_dynamic_programming\n"
                 << "optimal_success_probability=" << scientific << setprecision(15)
                 << probability.initial_probability << '\n'
                 << "optimal_success_probability_lower=" << scientific << setprecision(15)
                 << static_cast<double>(probability.initial_lower_scaled) /
                        static_cast<double>(PROBABILITY_SCALE) << '\n'
                 << "optimal_success_probability_upper=" << scientific << setprecision(15)
                 << static_cast<double>(probability.initial_upper_scaled) /
                        static_cast<double>(PROBABILITY_SCALE) << '\n'
                 << "probability_interval_width=" << scientific << setprecision(15)
                 << static_cast<double>(probability.initial_upper_scaled -
                                        probability.initial_lower_scaled) /
                        static_cast<double>(PROBABILITY_SCALE) << '\n'
                 << "optimal_success_percent=" << fixed << setprecision(12)
                 << probability.initial_probability * 100.0 << '\n'
                 << "possibly_positive_probability_states=" << probability.positive_states << '\n'
                 << "certified_certain_probability_states=" << probability.certain_states << '\n'
                 << "probability_elapsed_seconds=" << fixed << setprecision(6)
                 << probability.elapsed_seconds << '\n';
        }
        return result.complete || config.mode == "witness" ? 0 : 2;
    } catch (const exception& error) {
        cerr << "error: " << error.what() << '\n';
        return 1;
    }
}
