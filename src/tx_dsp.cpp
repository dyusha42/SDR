#include "tx_dsp.hpp"

#include <cmath>
#include <stdexcept>

#include "frame.hpp"

namespace sdr {

// BPSK: 1 -> +A, 0 -> -A на I, Q = 0
std::vector<int16_t> bpsk_iq(const std::vector<uint8_t> &bits) {
    std::vector<int16_t> s(2 * bits.size());
    for (size_t i = 0; i < bits.size(); ++i) {
        s[2 * i] = bits[i] ? SYMBOL_AMP : -SYMBOL_AMP;
        s[2 * i + 1] = 0;
    }
    return s;
}

// QPSK: первый бит задаёт знак I, второй знак Q (1 -> плюс)
std::vector<int16_t> qpsk_iq(const std::vector<uint8_t> &bits) {
    if (bits.size() % 2 != 0) throw std::runtime_error("qpsk_iq: нечётное число бит");
    std::vector<int16_t> s(bits.size());
    for (size_t i = 0; i < bits.size() / 2; ++i) {
        s[2 * i] = bits[2 * i] ? SYMBOL_AMP : -SYMBOL_AMP;
        s[2 * i + 1] = bits[2 * i + 1] ? SYMBOL_AMP : -SYMBOL_AMP;
    }
    return s;
}

std::vector<int16_t> preamble_iq() {
    const auto &p = preamble_symbols();
    std::vector<int16_t> s(2 * p.size());
    for (size_t i = 0; i < p.size(); ++i) {
        s[2 * i] = p[i] > 0 ? SYMBOL_AMP : -SYMBOL_AMP;
        s[2 * i + 1] = 0;
    }
    return s;
}

// после каждого символа sps-1 нулей
std::vector<int16_t> stuff_zeros(const std::vector<int16_t> &symbols_iq, int sps) {
    size_t n_sym = symbols_iq.size() / 2;
    std::vector<int16_t> out(2 * n_sym * static_cast<size_t>(sps), 0);
    for (size_t k = 0; k < n_sym; ++k) {
        out[2 * k * sps] = symbols_iq[2 * k];
        out[2 * k * sps + 1] = symbols_iq[2 * k + 1];
    }
    return out;
}

std::string tile_text(const std::string &text, size_t n) {
    std::string out;
    while (!text.empty() && out.size() < n) {
        if (!out.empty()) out += ' ';
        out += text;
    }
    if (out.size() > n) out.resize(n);
    return out;
}

std::vector<int16_t> convolve_iq(const std::vector<int16_t> &x_iq, const std::vector<int16_t> &h) {
    size_t n = x_iq.size() / 2;
    size_t l = h.size();
    if (n == 0 || l == 0) return {};
    size_t m = n + l - 1;
    std::vector<int32_t> acc(2 * m, 0);
    for (size_t i = 0; i < n; ++i) {
        for (size_t k = 0; k < l; ++k) {
            acc[2 * (i + k)] += static_cast<int32_t>(x_iq[2 * i]) * h[k];
            acc[2 * (i + k) + 1] += static_cast<int32_t>(x_iq[2 * i + 1]) * h[k];
        }
    }
    std::vector<int16_t> y(2 * m);
    for (size_t i = 0; i < 2 * m; ++i) {
        int32_t v = acc[i];
        if (v > 32767) v = 32767;
        if (v < -32768) v = -32768;
        y[i] = static_cast<int16_t>(v);
    }
    return y;
}

TxBurst build_tx_burst(const std::string &message, ModKind mod, size_t mtu, bool fill) {
    TxBurst b;
    b.mod = mod;

    size_t limit = max_message_len(mod, mtu);

    // режим fill: повторяем сообщение, пока не заполнится буфер
    std::string text = message;
    if (fill && !message.empty() && message.size() <= limit) text = tile_text(message, limit);
    b.message = text;
    if (text.size() > limit)
        throw std::runtime_error("сообщение (" + std::to_string(text.size()) + " байт) не помещается в буфер: для " +
                                 to_string(mod) + " максимум " + std::to_string(limit) + " байт");

    b.bits = bytes_to_bits(build_frame_bytes(text));
    std::vector<int16_t> data = (mod == ModKind::BPSK) ? bpsk_iq(b.bits) : qpsk_iq(b.bits);

    b.symbols = preamble_iq();
    b.symbols.insert(b.symbols.end(), data.begin(), data.end());
    b.n_syms = b.symbols.size() / 2;

    b.upsampled = stuff_zeros(b.symbols, SYM_LEN);

    // формирующий фильтр: прямоугольный импульс
    const std::vector<int16_t> h(SYM_LEN, 1);
    b.shaped = convolve_iq(b.upsampled, h);

    // хвост свёртки нулевой, его не копируем
    size_t sig_samples = b.n_syms * SYM_LEN;
    b.used_samples = TX_GUARD_SAMPLES + sig_samples;
    if (b.used_samples > mtu) throw std::runtime_error("кадр не помещается в буфер передачи");

    b.buffer.assign(2 * mtu, 0);
    for (size_t i = 0; i < 2 * sig_samples; ++i) b.buffer[2 * TX_GUARD_SAMPLES + i] = b.shaped[i];
    return b;
}

std::vector<int16_t> make_tone_burst(size_t mtu, double rate_hz, double tone_hz, int16_t amplitude) {
    std::vector<int16_t> buf(2 * mtu, 0);
    const double two_pi = 6.283185307179586;
    for (size_t n = TX_GUARD_SAMPLES; n + TX_GUARD_SAMPLES < mtu; ++n) {
        double ph = two_pi * tone_hz * static_cast<double>(n) / rate_hz;
        buf[2 * n] = static_cast<int16_t>(std::lround(amplitude * std::cos(ph)));
        buf[2 * n + 1] = static_cast<int16_t>(std::lround(amplitude * std::sin(ph)));
    }
    return buf;
}

}  // namespace sdr
