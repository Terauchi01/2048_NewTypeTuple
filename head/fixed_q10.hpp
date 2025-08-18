// fixed_q10.hpp
#pragma once
#include <cstdint>
#include <cmath>
#include <climits>
#include <algorithm>

struct q10 {
    int32_t v; // 内部表現（Q10）
    static constexpr int FRAC_BITS = 10;
    static constexpr int32_t Q = 1 << FRAC_BITS;       // 1024
    static constexpr int32_t FRAC_MASK = Q - 1;        // 0x3FF

    // --- 構築 ---
    constexpr q10() : v(0) {}
    static constexpr q10 from_raw(int32_t raw) { q10 t; t.v = raw; return t; }

    // 丸めあり（四捨五入）
    static inline q10 from_double(double x) {
        long long t = llround(x * Q);
        t = std::clamp<long long>(t, INT32_MIN, INT32_MAX);
        return from_raw((int32_t)t);
    }
    static inline q10 from_float(float x) {
        long long t = llround((double)x * Q);
        t = std::clamp<long long>(t, INT32_MIN, INT32_MAX);
        return from_raw((int32_t)t);
    }
    // 丸めなし（0方向に切り捨て）
    static inline q10 from_double_trunc(double x) {
        long long t = (long long)(x * Q);
        t = std::clamp<long long>(t, INT32_MIN, INT32_MAX);
        return from_raw((int32_t)t);
    }

    // 実数へ
    inline double to_double() const { return (double)v / Q; }
    inline float  to_float()  const { return (float)v / (float)Q; }

    // --- 基本演算 ---
    friend inline q10 operator+(q10 a, q10 b) { return from_raw(a.v + b.v); }
    friend inline q10 operator-(q10 a, q10 b) { return from_raw(a.v - b.v); }

    // 乗算（丸め最近隣）
    friend inline q10 operator*(q10 a, q10 b) {
        long long t = (long long)a.v * (long long)b.v;
        t += (t >= 0 ? Q/2 : -(Q/2));
        return from_raw((int32_t)(t >> FRAC_BITS));
    }

    // 除算（0方向でもOK。丸めたいなら調整してね）
    friend inline q10 operator/(q10 a, q10 b) {
        long long num = ((long long)a.v << FRAC_BITS);
        // ここでは単純に / でOK（丸めは用途に応じて追加）
        return from_raw((int32_t)(num / (long long)b.v));
    }

    // --- 小数部10bitの「抽出」 ---
    // 1) 生ビット（two's complementの下位10bit）そのまま
    inline uint16_t frac_bits_raw() const {
        return (uint16_t)(v & FRAC_MASK);
    }
    // 2) 数学的な「小数の大きさ」[0..1023]（負数でも常に非負の大きさ）
    inline uint16_t frac_bits_mag() const {
        // vをQで割った余りを [0, Q-1] に正規化
        int32_t r = v & FRAC_MASK;             // two's complementでの下位10bit
        if (v >= 0) return (uint16_t)r;
        // 負数のとき：小数の大きさは (Q - r) % Q
        return (uint16_t)((Q - r) & FRAC_MASK);
    }

    // 補助：整数部（数学的に切り下げ/切り上げなどは用途次第）
    inline int32_t integer_part_floor() const {
        // v/Q を0方向へ丸めるなら単純に v >> FRAC_BITS（負数は算術右シフト）
        return v >> FRAC_BITS;
    }
};

// --- ダイレクト関数（double/floatから小数部10bitだけ欲しい） ---
// 「Q10に変換→下位10bitを取り出す」＝固定小数点としての“生ビット”が欲しい場合
inline uint16_t frac10_raw_from_double(double x) {
    return (uint16_t)((int32_t)llround(x * q10::Q) & q10::FRAC_MASK);
}
inline uint16_t frac10_raw_from_float(float x) {
    return (uint16_t)((int32_t)llround((double)x * q10::Q) & q10::FRAC_MASK);
}

// --- 直接 Q10 (raw int32_t) へ変換するヘルパ ---
// 丸めあり（四捨五入）で double/float から Q10 の生ビット（int32）を得る
inline int32_t q10_raw_from_double(double x) {
    long long t = llround(x * q10::Q);
    t = std::clamp<long long>(t, (long long)INT32_MIN, (long long)INT32_MAX);
    return (int32_t)t;
}
inline int32_t q10_raw_from_float(float x) {
    long long t = llround((double)x * q10::Q);
    t = std::clamp<long long>(t, (long long)INT32_MIN, (long long)INT32_MAX);
    return (int32_t)t;
}

// 丸めなし（0方向に切り捨て）で変換するバージョン
inline int32_t q10_raw_from_double_trunc(double x) {
    long long t = (long long)(x * q10::Q);
    t = std::clamp<long long>(t, (long long)INT32_MIN, (long long)INT32_MAX);
    return (int32_t)t;
}
inline int32_t q10_raw_from_float_trunc(float x) {
    long long t = (long long)((double)x * q10::Q);
    t = std::clamp<long long>(t, (long long)INT32_MIN, (long long)INT32_MAX);
    return (int32_t)t;
}

// 「小数の大きさ（常に0..1023）」が欲しい場合（負のときも正の小数に）
inline uint16_t frac10_mag_from_double(double x) {
    double f = std::fmod(std::fabs(x), 1.0);            // [0,1)
    long long t = llround(f * q10::Q);                  // 0..Q
    if (t == q10::Q) t = 0;                             // 1.0に丸まったら0に折り返す
    return (uint16_t)t;
}
// 1) 任意の整数の「下位10bit」をそのまま小数に（0..1023 → [0, 1)）
inline double frac10_rawbits_to_double(uint32_t x) {
    return (double)(x & 0x3FFu) / 1024.0;   // 0x3FF = 1023
}

// 2) Q10値（int32）から「数学的な小数の大きさ」だけを取り出して double へ
//    負数でも常に [0,1) の小数を返す
inline double q10_frac_mag_to_double(int32_t q) {
    uint32_t r = (uint32_t)q & 0x3FFu;      // 下位10bit
    if (q >= 0) return (double)r / 1024.0;
    return (r == 0) ? 0.0 : (double)(1024 - r) / 1024.0;
}

// 3) 参考：Q10 全体（整数部＋小数部）を double へ
inline double q10_to_double(int32_t q) {
    return (double)q / 1024.0;
}