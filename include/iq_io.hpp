// файлы IQ: int16, чередование I/Q
#pragma once

#include <string>
#include <vector>

#include "common.hpp"

namespace sdr {

void write_pcm(const std::string &path, const std::vector<int16_t> &iq);
std::vector<int16_t> read_pcm(const std::string &path);

}  // namespace sdr
