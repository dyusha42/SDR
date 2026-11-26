#pragma once

#include <complex>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace sdr {

// отсчётов на символ
constexpr int SYM_LEN = 10;

// амплитуда символа (12 бит ЦАП, сдвиг на 4)
constexpr int16_t SYMBOL_AMP = 300 << 4;

// частота дискретизации по умолчанию, Гц
constexpr double DEFAULT_RATE = 1e6;

// размер буфера Pluto, отсчётов
constexpr size_t DEFAULT_MTU = 1920;

// на сколько вперёд планируем передачу, нс
constexpr long long TX_ADVANCE_NS = 4LL * 1000 * 1000;

using cf = std::complex<float>;

enum class ModKind { BPSK, QPSK };

inline const char *to_string(ModKind m) { return m == ModKind::BPSK ? "bpsk" : "qpsk"; }

bool parse_mod_kind(const std::string &s, ModKind &out);

}  // namespace sdr
