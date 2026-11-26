#include "live_data.hpp"

#include <algorithm>

#include "frame.hpp"

namespace sdr {

void LiveData::push_samples(const int16_t *iq, size_t n) {
    std::lock_guard<std::mutex> lock(m_);
    for (size_t i = 0; i < n; ++i) {
        win_i_.push_back(static_cast<float>(iq[2 * i]));
        win_q_.push_back(static_cast<float>(iq[2 * i + 1]));
    }
    total_ += n;
    ++buffers_;
    // оставляем только последние WINDOW отсчётов
    if (win_i_.size() > WINDOW) {
        const long cut = static_cast<long>(win_i_.size() - WINDOW);
        win_i_.erase(win_i_.begin(), win_i_.begin() + cut);
        win_q_.erase(win_q_.begin(), win_q_.begin() + cut);
    }
}

void LiveData::push_frame(const Frame &f) {
    std::lock_guard<std::mutex> lock(m_);

    // участок окна с кадром (с запасом по краям)
    const uint64_t win_start = total_ - win_i_.size();
    const uint64_t margin = 2 * SYM_LEN;
    const uint64_t a = std::max<uint64_t>(f.start_sample > margin ? f.start_sample - margin : 0, win_start);
    const uint64_t b = std::min<uint64_t>(f.end_sample + margin, total_);
    if (b > a) {
        std::vector<cf> x;
        for (uint64_t n = a; n < b; ++n) x.emplace_back(win_i_[n - win_start], win_q_[n - win_start]);
        const std::vector<cf> y = boxcar_mf(x);
        frame_i_.clear();
        frame_q_.clear();
        frame_raw_i_.clear();
        frame_raw_q_.clear();
        for (const cf &v : y) {
            frame_i_.push_back(v.real());
            frame_q_.push_back(v.imag());
        }
        for (const cf &v : x) {
            frame_raw_i_.push_back(v.real());
            frame_raw_q_.push_back(v.imag());
        }
        // первый символьный отсчёт: конец первого символа преамбулы
        const int64_t first = static_cast<int64_t>(f.start_sample) + SYM_LEN - 1 - static_cast<int64_t>(a);
        sym_first_ = (first >= 0 && first < static_cast<int64_t>(y.size())) ? static_cast<int>(first) : -1;
        sym_count_ = static_cast<int>(f.corr_symbols.size());
        amp_frame_ = static_cast<float>(f.amplitude);
        theta_ = f.theta_rad;
        phase_ = f.phase_rad;
    }

    // точки созвездия данных (без преамбулы)
    auto data_part = [](const std::vector<cf> &v) {
        return std::vector<cf>(v.begin() + std::min<size_t>(PREAMBLE_SYMBOLS, v.size()), v.end());
    };
    raw_frames_.push_back(data_part(f.raw_symbols));
    cor_frames_.push_back(data_part(f.corr_symbols));
    if (raw_frames_.size() > KEEP_FRAMES) raw_frames_.pop_front();
    if (cor_frames_.size() > KEEP_FRAMES) cor_frames_.pop_front();

    last_text_ = f.text;
    cfo_ = f.cfo_hz;
    snr_ = f.snr_db;
    amp_ = f.amplitude;
}

void LiveData::set_counts(size_t ok, size_t bad) {
    std::lock_guard<std::mutex> lock(m_);
    ok_ = ok;
    bad_ = bad;
}

LiveSnapshot LiveData::snapshot() {
    std::lock_guard<std::mutex> lock(m_);
    LiveSnapshot s;
    s.win_i = win_i_;
    s.win_q = win_q_;
    s.frame_i = frame_i_;
    s.frame_q = frame_q_;
    s.frame_raw_i = frame_raw_i_;
    s.frame_raw_q = frame_raw_q_;
    s.sym_first = sym_first_;
    s.sym_count = sym_count_;
    s.amp = amp_frame_ > 1e-3f ? amp_frame_ : 10.f;
    s.theta = theta_;
    s.phase = phase_;
    for (const auto &fr : raw_frames_)
        for (const cf &v : fr) {
            s.raw_x.push_back(v.real());
            s.raw_y.push_back(v.imag());
        }
    for (const auto &fr : cor_frames_)
        for (const cf &v : fr) {
            s.cor_x.push_back(v.real());
            s.cor_y.push_back(v.imag());
        }
    s.buffers = buffers_;
    s.frames_ok = ok_;
    s.frames_bad = bad_;
    s.last_text = last_text_;
    s.cfo_hz = cfo_;
    s.snr_db = snr_;
    s.amplitude = amp_;
    return s;
}

}  // namespace sdr
