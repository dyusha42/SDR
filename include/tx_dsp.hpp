// цепочка передатчика, IQ хранится чередованием: I0, Q0, I1, Q1, ...
#pragma once

#include <string>
#include <vector>

#include "common.hpp"

namespace sdr {

// нули в начале буфера (драйвер может использовать начало под метку времени)
constexpr size_t TX_GUARD_SAMPLES = 2 * SYM_LEN;

std::vector<int16_t> bpsk_iq(const std::vector<uint8_t> &bits);
std::vector<int16_t> qpsk_iq(const std::vector<uint8_t> &bits);
std::vector<int16_t> preamble_iq();

std::vector<int16_t> stuff_zeros(const std::vector<int16_t> &symbols_iq, int sps);

// сообщение, повторённое через пробел до длины n (для режима fill)
std::string tile_text(const std::string &text, size_t n);

// свёртка IQ с вещественной h, длина результата N + len(h) - 1
std::vector<int16_t> convolve_iq(const std::vector<int16_t> &x_iq, const std::vector<int16_t> &h);

struct TxBurst {
    std::string message;
    ModKind mod = ModKind::BPSK;
    std::vector<uint8_t> bits;       // биты кадра без преамбулы
    std::vector<int16_t> symbols;    // символы с преамбулой
    std::vector<int16_t> upsampled;  // после вставки нулей
    std::vector<int16_t> shaped;     // после свёртки
    std::vector<int16_t> buffer;     // буфер ЦАП
    size_t n_syms = 0;
    size_t used_samples = 0;
};

// бросает исключение, если сообщение не помещается в буфер
// fill = повторять сообщение по кругу, пока не заполнится весь буфер
TxBurst build_tx_burst(const std::string &message, ModKind mod, size_t mtu, bool fill = false);

// тестовый тон для лабы 2
std::vector<int16_t> make_tone_burst(size_t mtu, double rate_hz, double tone_hz, int16_t amplitude);

}  // namespace sdr
