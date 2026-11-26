#include "iq_io.hpp"

#include <cstdio>
#include <stdexcept>

namespace sdr {

void write_pcm(const std::string &path, const std::vector<int16_t> &iq) {
    FILE *f = std::fopen(path.c_str(), "wb");
    if (!f) throw std::runtime_error("не удалось открыть файл для записи: " + path);
    size_t w = iq.empty() ? 0 : std::fwrite(iq.data(), sizeof(int16_t), iq.size(), f);
    int rc = std::fclose(f);
    if (w != iq.size() || rc != 0) throw std::runtime_error("ошибка записи файла: " + path);
}

std::vector<int16_t> read_pcm(const std::string &path) {
    FILE *f = std::fopen(path.c_str(), "rb");
    if (!f) throw std::runtime_error("не удалось открыть файл: " + path);
    std::vector<int16_t> data;
    int16_t buf[4096];
    size_t r;
    while ((r = std::fread(buf, sizeof(int16_t), 4096, f)) > 0) data.insert(data.end(), buf, buf + r);
    std::fclose(f);
    if (data.size() % 2 != 0) data.pop_back();  // обрезанная запись
    return data;
}

}  // namespace sdr
