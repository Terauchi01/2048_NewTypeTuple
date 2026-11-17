#ifndef NT9_COMPACT_LAYOUT_H
#define NT9_COMPACT_LAYOUT_H
#include <array>
#include <cstddef>
#include <stdexcept>

namespace nt9_compact {
constexpr std::size_t power(std::size_t base, int digits) {
  std::size_t result = 1;
  while (digits-- > 0) result *= base;
  return result;
}
// The stage is selected on the original board, before filtering.
constexpr int bases[2][4] = {{7, 7, 6, 2}, {7, 7, 7, 6}};
struct Layout {
  int digits;
  std::size_t dense_length;
  std::array<std::array<std::size_t, 4>, 2> offsets{};
  std::array<std::array<std::size_t, 4>, 2> lengths{};
  std::size_t count = 0;
  explicit Layout(int n) : digits(n), dense_length(power(7, n)) {
    for (int s = 0; s < 2; ++s)
      for (int f = 0; f < 4; ++f) {
        offsets[s][f] = count;
        lengths[s][f] = power(bases[s][f], digits);
        count += 7 * lengths[s][f];
      }
  }
  std::size_t row(int s, int f, int t) const {
    return offsets[s][f] + t * lengths[s][f];
  }
  // Legacy base-7 index -> compact row index; false means unreachable.
  bool compact_index(int s, int f, std::size_t dense, std::size_t& out) const {
    const int base = bases[s][f];
    if (base == 7) { out = dense; return dense < dense_length; }
    out = 0;
    std::size_t place = 1;
    for (int k = 0; k < digits; ++k) {
      const auto digit = dense % 7;
      if (digit >= static_cast<std::size_t>(base)) return false;
      out += digit * place;
      place *= base;
      dense /= 7;
    }
    return dense == 0;
  }
  std::size_t dense_index(int s, int f, std::size_t compact) const {
    std::size_t result = 0, place = 1;
    for (int k = 0; k < digits; ++k) {
      result += (compact % bases[s][f]) * place;
      compact /= bases[s][f];
      place *= 7;
    }
    return result;
  }
};
}
#endif
