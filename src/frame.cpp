#include "frame.hpp"

#include <stdexcept>

namespace sdr {

const std::vector<int> &preamble_symbols() {
    static const std::vector<int> p = [] {
        // m-последовательность 31 бит (x^5 + x^3 + 1) плюс повтор первого
        std::vector<int> seq;
        unsigned reg = 0x01;
        for (int i = 0; i < 31; ++i) {
            seq.push_back((reg & 1u) ? +1 : -1);
            unsigned fb = ((reg >> 0) ^ (reg >> 2)) & 1u;
            reg = (reg >> 1) | (fb << 4);
        }
        seq.push_back(seq[0]);
        return seq;
    }();
    return p;
}

uint16_t crc16_ccitt(const uint8_t *data, size_t len) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; ++i) {
        crc ^= static_cast<uint16_t>(data[i]) << 8;
        for (int b = 0; b < 8; ++b) crc = (crc & 0x8000) ? static_cast<uint16_t>((crc << 1) ^ 0x1021) : static_cast<uint16_t>(crc << 1);
    }
    return crc;
}

std::vector<uint8_t> build_frame_bytes(const std::string &payload) {
    if (payload.size() > MAX_PAYLOAD) throw std::runtime_error("сообщение длиннее 255 байт");
    std::vector<uint8_t> out;
    out.push_back(static_cast<uint8_t>(payload.size()));
    for (unsigned char c : payload) out.push_back(c);
    uint16_t crc = crc16_ccitt(out.data(), out.size());
    out.push_back(static_cast<uint8_t>(crc >> 8));
    out.push_back(static_cast<uint8_t>(crc & 0xFF));
    return out;
}

// старший бит первым
std::vector<uint8_t> bytes_to_bits(const std::vector<uint8_t> &bytes) {
    std::vector<uint8_t> bits;
    bits.reserve(bytes.size() * 8);
    for (uint8_t b : bytes)
        for (int j = 7; j >= 0; --j) bits.push_back((b >> j) & 1u);
    return bits;
}

std::vector<uint8_t> bits_to_bytes(const std::vector<uint8_t> &bits) {
    std::vector<uint8_t> bytes(bits.size() / 8, 0);
    for (size_t i = 0; i + 8 <= bits.size(); i += 8) {
        uint8_t v = 0;
        for (int j = 0; j < 8; ++j) v = static_cast<uint8_t>((v << 1) | (bits[i + j] & 1u));
        bytes[i / 8] = v;
    }
    return bytes;
}

size_t data_symbols_count(size_t payload_len, ModKind mod) {
    size_t bits = (payload_len + 3) * 8;  // длина + данные + CRC
    return mod == ModKind::BPSK ? bits : bits / 2;
}

size_t max_message_len(ModKind mod, size_t mtu) {
    // в буфер входят защитный интервал (2 символа), преамбула и данные
    size_t guard_symbols = 2;
    size_t symbols_total = mtu / SYM_LEN;
    if (symbols_total <= guard_symbols + PREAMBLE_SYMBOLS) return 0;
    size_t free_symbols = symbols_total - guard_symbols - PREAMBLE_SYMBOLS;
    size_t bits = free_symbols * (mod == ModKind::BPSK ? 1 : 2);
    size_t bytes = bits / 8;
    if (bytes < 3) return 0;
    size_t m = bytes - 3;
    return m > MAX_PAYLOAD ? MAX_PAYLOAD : m;
}

}  // namespace sdr
