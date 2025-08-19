#include <iostream>

int main() {
    const int MAX_TILE_VALUE = 17; // 0..17
    const int NUM_SPLIT = 4;

    auto map_value = [&](int f, int v) -> int {
        if (f == 0) {
            if (v == 0) return 0;
            else if (v > 5) return 6;
            else return v;
        } else if (f == 1) {
            if (v == 0) return 0;
            else if (v <= 5) return 1;
            else if (v > 9) return 6;
            else return v - 4;
        } else if (f == 2) {
            if (v == 0) return 0;
            else if (v <= 9) return 1;
            else if (v > 13) return 6;
            else return v - 8;
        } else { // f == 3
            if (v == 0) return 0;
            else if (v < 14) return 1;
            else return v - 12;
        }
    };

    std::cout << "// auto-generated filter mapping\n";
    std::cout << "static const uint8_t FILTER_MAPPING[" << NUM_SPLIT << "][" << (MAX_TILE_VALUE+1) << "] = {\n";
    for (int f = 0; f < NUM_SPLIT; ++f) {
        std::cout << "  { ";
        for (int v = 0; v <= MAX_TILE_VALUE; ++v) {
            std::cout << map_value(f, v);
            if (v != MAX_TILE_VALUE) std::cout << ", ";
        }
        std::cout << " }";
        if (f != NUM_SPLIT-1) std::cout << ",";
        std::cout << "\n";
    }
    std::cout << "};\n";
    return 0;
}