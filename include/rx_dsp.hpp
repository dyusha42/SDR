// цепочка приёмника: согласованный фильтр, поиск кадра, коррекция, демодуляция
#pragma once

#include <string>
#include <vector>

#include "common.hpp"

namespace sdr {

std::vector<cf> to_complex(const int16_t *iq, size_t n_complex);

// скользящее среднее по длине символа
std::vector<cf> boxcar_mf(const std::vector<cf> &x);

struct Frame {
    uint64_t start_sample = 0;     // первый отсчёт кадра в записи
    uint64_t end_sample = 0;       // отсчёт сразу после кадра
    double cfo_hz = 0;             // расстройка частоты
    double snr_db = 0;
    double sync_metric = 0;        // корреляция с преамбулой, 0..1
    double amplitude = 0;          // амплитуда сигнала в отсчётах АЦП
    float theta_rad = 0;           // поворот фазы за один символ (расстройка частоты)
    float phase_rad = 0;           // начальная фаза после компенсации частоты
    std::string text;
    std::vector<cf> raw_symbols;   // до коррекции
    std::vector<cf> corr_symbols;  // после коррекции
};

// потоковый декодер: кормим буферами, получаем кадры
class StreamDecoder {
public:
    StreamDecoder(ModKind mod, double sample_rate_hz, size_t max_frame_samples = 2 * DEFAULT_MTU);

    std::vector<Frame> feed(const int16_t *iq, size_t n_complex);

    size_t frames_ok() const { return ok_; }
    size_t frames_bad() const { return bad_; }

    float sync_threshold = 0.70f;  // порог корреляции с преамбулой
    float min_power = 4.0f;        // ниже этой мощности считаем шумом

private:
    ModKind mod_;
    double fs_;
    size_t max_frame_samples_;
    std::vector<cf> buf_;
    uint64_t base_ = 0;    // номер первого отсчёта в buf_
    size_t scan_pos_ = 0;  // с какого индекса продолжать поиск
    size_t ok_ = 0, bad_ = 0;
};

}  // namespace sdr
