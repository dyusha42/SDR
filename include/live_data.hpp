// данные для живого окна: поток приёма пишет, окно читает
#pragma once

#include <deque>
#include <mutex>
#include <string>
#include <vector>

#include "common.hpp"
#include "rx_dsp.hpp"

namespace sdr {

// копия данных для отрисовки
struct LiveSnapshot {
    std::vector<float> win_i, win_q;        // последние принятые отсчёты
    std::vector<float> frame_i, frame_q;    // последний кадр после согласованного фильтра
    std::vector<float> frame_raw_i, frame_raw_q;  // тот же участок до фильтра
    int sym_first = -1;                     // индекс первого символьного отсчёта в массивах кадра (-1 = нет данных)
    int sym_count = 0;                      // символов в кадре, включая преамбулу
    float amp = 10.f;                       // амплитуда последнего кадра (для нормировки)
    float theta = 0.f, phase = 0.f;         // поворот за символ и начальная фаза последнего кадра
    std::vector<float> raw_x, raw_y;        // созвездие до коррекции
    std::vector<float> cor_x, cor_y;        // созвездие после коррекции
    size_t buffers = 0, frames_ok = 0, frames_bad = 0;
    std::string last_text;
    double cfo_hz = 0, snr_db = 0, amplitude = 0;
};

class LiveData {
public:
    static constexpr size_t WINDOW = 8192;  // сколько последних отсчётов хранить
    static constexpr size_t KEEP_FRAMES = 10;

    void push_samples(const int16_t *iq, size_t n_complex);
    void push_frame(const Frame &f);
    void set_counts(size_t ok, size_t bad);

    LiveSnapshot snapshot();

private:
    std::mutex m_;
    std::vector<float> win_i_, win_q_;
    uint64_t total_ = 0;  // сколько отсчётов принято всего
    std::vector<float> frame_i_, frame_q_, frame_raw_i_, frame_raw_q_;
    int sym_first_ = -1, sym_count_ = 0;
    float amp_frame_ = 10.f, theta_ = 0.f, phase_ = 0.f;
    std::deque<std::vector<cf>> raw_frames_, cor_frames_;
    size_t buffers_ = 0, ok_ = 0, bad_ = 0;
    std::string last_text_;
    double cfo_ = 0, snr_ = 0, amp_ = 0;
};

}  // namespace sdr
