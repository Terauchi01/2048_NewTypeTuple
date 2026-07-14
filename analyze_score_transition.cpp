#include <algorithm>
#include <array>
#include <charconv>
#include <cerrno>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#ifdef _OPENMP
#include <omp.h>
#endif

namespace {

constexpr std::array<int, 7> kTurns = {220, 450, 900, 1700, 3600, 7200, 14500};

struct FileInfo {
    std::string path;
    int tuple = 0;
    int seed = 0;
};

struct FileSeries {
    std::string path;
    int tuple = 0;
    int seed = 0;
    std::array<std::vector<double>, kTurns.size()> series;
};

struct AggregateSeries {
    std::vector<double> sum;
    std::vector<int> count;
};

struct TupleAggregate {
    std::array<AggregateSeries, kTurns.size()> series;
};

struct WindowState {
    explicit WindowState(std::size_t window) : buffer(window, 0) {}

    void add(int score, std::size_t window) {
        if (window == 0) {
            return;
        }

        if (filled < window) {
            buffer[filled] = score;
            sum += score;
            ++filled;
            if (filled == window) {
                samples.push_back(static_cast<double>(sum) / static_cast<double>(window));
            }
            return;
        }

        const std::size_t pos = index % window;
        sum += score - buffer[pos];
        buffer[pos] = score;
        ++index;

        if (index % window == 0) {
            samples.push_back(static_cast<double>(sum) / static_cast<double>(window));
        }
    }

    std::vector<int> buffer;
    std::size_t index = 0;
    std::size_t filled = 0;
    long long sum = 0;
    std::vector<double> samples;
};

bool has_suffix(const std::string& value, std::string_view suffix) {
    return value.size() >= suffix.size() &&
           value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
}

bool parse_int(const char*& p, const char* end, int& out) {
    const char* begin = p;
    auto result = std::from_chars(begin, end, out);
    if (result.ec != std::errc()) {
        return false;
    }
    p = result.ptr;
    return true;
}

bool consume(const char*& p, const char* end, std::string_view token) {
    if (static_cast<std::size_t>(end - p) < token.size()) {
        return false;
    }
    if (std::memcmp(p, token.data(), token.size()) != 0) {
        return false;
    }
    p += token.size();
    return true;
}

bool parse_filename(const std::string& name, int& tuple, int& seed) {
    if (!has_suffix(name, ".log")) {
        return false;
    }

    const std::string_view stem(name.data(), name.size() - 4);
    const std::string_view marker = "tuple_seed";
    const std::size_t pos = stem.find(marker);
    if (pos == std::string_view::npos) {
        return false;
    }

    const std::string_view tuple_part = stem.substr(0, pos);
    const std::string_view seed_part = stem.substr(pos + marker.size());
    if (tuple_part.empty() || seed_part.empty()) {
        return false;
    }

    const auto tuple_result = std::from_chars(tuple_part.data(), tuple_part.data() + tuple_part.size(), tuple);
    const auto seed_result = std::from_chars(seed_part.data(), seed_part.data() + seed_part.size(), seed);
    return tuple_result.ec == std::errc() && seed_result.ec == std::errc();
}

std::vector<FileInfo> collect_log_files(const std::string& dir_path) {
    std::vector<FileInfo> files;

    DIR* dir = opendir(dir_path.c_str());
    if (dir == nullptr) {
        return files;
    }

    while (dirent* entry = readdir(dir)) {
        const std::string name = entry->d_name;
        if (name == "." || name == "..") {
            continue;
        }
        if (!has_suffix(name, ".log")) {
            continue;
        }

        int tuple = 0;
        int seed = 0;
        if (!parse_filename(name, tuple, seed)) {
            continue;
        }

        files.push_back(FileInfo{dir_path + "/" + name, tuple, seed});
    }

    closedir(dir);

    std::sort(files.begin(), files.end(), [](const FileInfo& lhs, const FileInfo& rhs) {
        if (lhs.tuple != rhs.tuple) {
            return lhs.tuple < rhs.tuple;
        }
        if (lhs.seed != rhs.seed) {
            return lhs.seed < rhs.seed;
        }
        return lhs.path < rhs.path;
    });

    return files;
}

std::array<std::vector<double>, kTurns.size()> process_file(const std::string& path, std::size_t window) {
    std::array<WindowState, kTurns.size()> states = {
        WindowState(window), WindowState(window), WindowState(window),
        WindowState(window), WindowState(window), WindowState(window),
        WindowState(window)
    };

    std::ifstream in(path);
    std::string line;

    while (std::getline(in, line)) {
        if (line.size() < 5 || line.compare(0, 5, "game,") != 0) {
            continue;
        }

        const char* p = line.c_str();
        const char* end = p + line.size();

        int game = 0;
        int turn = 0;
        int score = 0;
        int big = 0;

        if (!consume(p, end, "game,")) {
            continue;
        }
        if (!parse_int(p, end, game) || p >= end || *p != ',') {
            continue;
        }
        ++p;
        if (!consume(p, end, "turn,")) {
            continue;
        }
        if (!parse_int(p, end, turn) || p >= end || *p != ',') {
            continue;
        }
        ++p;
        if (!consume(p, end, "sco,")) {
            continue;
        }
        if (!parse_int(p, end, score) || p >= end || *p != ',') {
            continue;
        }
        ++p;
        if (!consume(p, end, "big,")) {
            continue;
        }
        if (!parse_int(p, end, big)) {
            continue;
        }

        (void)game;
        (void)big;

        for (std::size_t i = 0; i < kTurns.size(); ++i) {
            if (turn == kTurns[i]) {
                states[i].add(score, window);
                break;
            }
        }
    }

    std::array<std::vector<double>, kTurns.size()> series;
    for (std::size_t i = 0; i < kTurns.size(); ++i) {
        series[i] = std::move(states[i].samples);
    }
    return series;
}

void merge_result(TupleAggregate& aggregate, const FileSeries& file_series) {
    for (std::size_t turn_index = 0; turn_index < kTurns.size(); ++turn_index) {
        const std::vector<double>& samples = file_series.series[turn_index];
        AggregateSeries& dest = aggregate.series[turn_index];

        if (dest.sum.size() < samples.size()) {
            dest.sum.resize(samples.size(), 0.0);
            dest.count.resize(samples.size(), 0);
        }

        for (std::size_t i = 0; i < samples.size(); ++i) {
            dest.sum[i] += samples[i];
            dest.count[i] += 1;
        }
    }
}

void write_csv(const std::string& output_path, const std::map<int, TupleAggregate>& aggregates, std::size_t window) {
    std::ofstream out(output_path);
    out << "tuple,turn,sample_index,games_processed,mean_score,file_count\n";

    for (const auto& [tuple, aggregate] : aggregates) {
        (void)tuple;
        for (std::size_t turn_index = 0; turn_index < kTurns.size(); ++turn_index) {
            const AggregateSeries& series = aggregate.series[turn_index];
            for (std::size_t i = 0; i < series.sum.size(); ++i) {
                if (series.count[i] == 0) {
                    continue;
                }
                const double mean = series.sum[i] / static_cast<double>(series.count[i]);
                out << tuple << ','
                    << kTurns[turn_index] << ','
                    << (i + 1) << ','
                    << ((i + 1) * window) << ','
                    << mean << ','
                    << series.count[i] << '\n';
            }
        }
    }
}

}  // namespace

int main(int argc, char** argv) {
    std::string logs_dir = "learn_double/logs";
    std::string output_path = "score_transition.csv";
    std::size_t window = 10000;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--logs-dir" && i + 1 < argc) {
            logs_dir = argv[++i];
        } else if (arg == "--output" && i + 1 < argc) {
            output_path = argv[++i];
        } else if (arg == "--window" && i + 1 < argc) {
            const long long parsed = std::strtoll(argv[++i], nullptr, 10);
            if (parsed > 0) {
                window = static_cast<std::size_t>(parsed);
            }
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "Usage: " << argv[0]
                      << " [--logs-dir DIR] [--output FILE] [--window N]\n"
                      << "  default logs-dir: learn_double/logs\n"
                      << "  default output  : score_transition.csv\n"
                      << "  default window  : 10000\n";
            return 0;
        }
    }

    const std::vector<FileInfo> files = collect_log_files(logs_dir);
    if (files.empty()) {
        std::cerr << "No log files found in " << logs_dir << '\n';
        return 1;
    }

    std::vector<FileSeries> processed(files.size());

#pragma omp parallel for schedule(dynamic)
    for (int i = 0; i < static_cast<int>(files.size()); ++i) {
        const FileInfo& file = files[static_cast<std::size_t>(i)];
        FileSeries result;
        result.path = file.path;
        result.tuple = file.tuple;
        result.seed = file.seed;
        result.series = process_file(file.path, window);
        processed[static_cast<std::size_t>(i)] = std::move(result);
    }

    std::map<int, TupleAggregate> aggregates;
    for (const FileSeries& file_series : processed) {
        merge_result(aggregates[file_series.tuple], file_series);
    }

    write_csv(output_path, aggregates, window);

    std::cout << "processed " << processed.size() << " files\n";
    std::cout << "wrote " << output_path << '\n';
    return 0;
}