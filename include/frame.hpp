// формат кадра: [преамбула 32 символа][длина][данные][CRC16]
#pragma once

#include <string>
#include <vector>

#include "common.hpp"

namespace sdr {

constexpr int PREAMBLE_SYMBOLS = 32;
constexpr size_t MAX_PAYLOAD = 255;

// известная приёмнику последовательность из +1/-1
const std::vector<int> &preamble_symbols();

uint16_t crc16_ccitt(const uint8_t *data, size_t len);

// байты кадра: длина, данные, CRC
std::vector<uint8_t> build_frame_bytes(const std::string &payload);

std::vector<uint8_t> bytes_to_bits(const std::vector<uint8_t> &bytes);

std::vector<uint8_t> bits_to_bytes(const std::vector<uint8_t> &bits);

size_t data_symbols_count(size_t payload_len, ModKind mod);

// максимальная длина текста для одного буфера
size_t max_message_len(ModKind mod, size_t mtu);

}  // namespace sdr
