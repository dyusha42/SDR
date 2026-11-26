#include "rx_dsp.hpp"

#include <algorithm>
#include <cmath>

#include "frame.hpp"

namespace sdr {

namespace {

constexpr float kPi = 3.14159265358979f;

enum class Status { Ok, Incomplete, Bad };

// корреляция разностей соседних символов с преамбулой (не зависит от расстройки частоты)
float sync_metric(const std::vector<cf> &y, size_t t, float min_power) {
    const auto &p = preamble_symbols();
    const size_t P = p.size();
    cf S(0.f, 0.f);
    float E = 0.f;
    float pw[PREAMBLE_SYMBOLS];
    for (size_t k = 0; k < P; ++k) pw[k] = std::norm(y[t + k * SYM_LEN]);
    for (size_t k = 0; k + 1 < P; ++k) {
        S += y[t + (k + 1) * SYM_LEN] * std::conj(y[t + k * SYM_LEN]) * static_cast<float>(p[k] * p[k + 1]);
        E += 0.5f * (pw[k] + pw[k + 1]);
    }
    if (E < min_power * static_cast<float>(P - 1)) return 0.f;

    // преамбула имеет постоянную амплитуду, окно должно целиком лежать на ней
    const float mean = E / static_cast<float>(P - 1);
    size_t strong = 0;
    for (size_t k = 0; k < P; ++k)
        if (pw[k] > 0.25f * mean) ++strong;
    if (strong < P - 3) return 0.f;
    return std::abs(S) / E;
}

Status try_decode(const std::vector<cf> &y, size_t t, ModKind mod, double fs, size_t max_frame_samples, float min_power,
                  Frame &f, size_t &end_y) {
    const auto &p = preamble_symbols();
    const size_t P = p.size();
    const size_t n = y.size();
    if (t + (P - 1) * SYM_LEN >= n) return Status::Incomplete;

    // расстройка частоты: угол суммы разностей = поворот за символ
    cf S(0.f, 0.f);
    float E = 0.f;
    for (size_t k = 0; k + 1 < P; ++k) {
        const cf a = y[t + k * SYM_LEN];
        const cf b = y[t + (k + 1) * SYM_LEN];
        S += b * std::conj(a) * static_cast<float>(p[k] * p[k + 1]);
        E += 0.5f * (std::norm(a) + std::norm(b));
    }
    const float theta = std::arg(S);

    // начальная фаза и амплитуда
    cf c(0.f, 0.f);
    for (size_t k = 0; k < P; ++k)
        c += y[t + k * SYM_LEN] * std::polar(1.0f, -theta * static_cast<float>(k)) * static_cast<float>(p[k]);
    const float phi = std::arg(c);
    const float A = std::abs(c) / static_cast<float>(P);
    if (!(A * A > min_power * 0.25f)) return Status::Bad;
    const cf rot0 = std::polar(1.0f / A, -phi);

    f = Frame();
    f.amplitude = A;
    f.theta_rad = theta;
    f.phase_rad = phi;
    f.sync_metric = (E > 0.f) ? std::abs(S) / E : 0.0;
    f.cfo_hz = static_cast<double>(theta) / (2.0 * kPi) * (fs / SYM_LEN);
    for (size_t k = 0; k < P; ++k) {
        const cf z = y[t + k * SYM_LEN];
        f.raw_symbols.push_back(z / A);
        f.corr_symbols.push_back(z * std::polar(1.0f, -theta * static_cast<float>(k)) * rot0);
    }

    // демодуляция данных с подстройкой фазы (петля Костаса)
    const int bps = (mod == ModKind::BPSK) ? 1 : 2;
    const float alpha = 0.05f, beta = alpha * alpha / 4.0f;
    float ph = 0.f, fr = 0.f;
    std::vector<uint8_t> bits;
    size_t need_bits = 0, nsym = 0;
    size_t k = P;
    while (true) {
        // после первого байта известна длина кадра
        if (need_bits == 0 && bits.size() >= 8) {
            unsigned len = 0;
            for (int j = 0; j < 8; ++j) len = (len << 1) | bits[j];
            need_bits = (static_cast<size_t>(len) + 3) * 8;
            nsym = need_bits / bps;
            if ((P + nsym) * SYM_LEN > max_frame_samples) return Status::Bad;
        }
        if (need_bits != 0 && bits.size() >= need_bits) break;

        const size_t idx = t + k * SYM_LEN;
        if (idx >= n) return Status::Incomplete;
        const cf z = y[idx];
        f.raw_symbols.push_back(z / A);
        const cf w = z * std::polar(1.0f, -theta * static_cast<float>(k)) * rot0 * std::polar(1.0f, -ph);
        cf d;
        if (mod == ModKind::BPSK) {
            d = cf(w.real() < 0 ? -1.f : 1.f, 0.f);
            bits.push_back(w.real() > 0 ? 1 : 0);
        } else {
            d = cf(w.real() < 0 ? -1.f : 1.f, w.imag() < 0 ? -1.f : 1.f);
            bits.push_back(w.real() > 0 ? 1 : 0);
            bits.push_back(w.imag() > 0 ? 1 : 0);
        }
        const float err = std::arg(w * std::conj(d));
        fr += beta * err;
        ph += fr + alpha * err;
        f.corr_symbols.push_back(w);
        ++k;
    }

    // проверка CRC
    const std::vector<uint8_t> bytes = bits_to_bytes(bits);
    const size_t len = bytes[0];
    const uint16_t crc_rx = static_cast<uint16_t>((bytes[len + 1] << 8) | bytes[len + 2]);
    const uint16_t crc = crc16_ccitt(bytes.data(), len + 1);
    if (crc != crc_rx) return Status::Bad;
    f.text.assign(bytes.begin() + 1, bytes.begin() + 1 + static_cast<long>(len));

    // SNR по разбросу точек вокруг идеальных
    double pe = 0.0, ps = 0.0;
    for (size_t i = P; i < f.corr_symbols.size(); ++i) {
        const cf w = f.corr_symbols[i];
        cf d;
        if (mod == ModKind::BPSK)
            d = cf(w.real() < 0 ? -1.f : 1.f, 0.f);
        else
            d = cf(w.real() < 0 ? -1.f : 1.f, w.imag() < 0 ? -1.f : 1.f);
        pe += std::norm(w - d);
        ps += std::norm(d);
    }
    f.snr_db = 10.0 * std::log10(ps / std::max(pe, 1e-9));

    end_y = t + (P + nsym - 1) * SYM_LEN;
    return Status::Ok;
}

}  // namespace

std::vector<cf> to_complex(const int16_t *iq, size_t n_complex) {
    std::vector<cf> x(n_complex);
    for (size_t i = 0; i < n_complex; ++i) x[i] = cf(static_cast<float>(iq[2 * i]), static_cast<float>(iq[2 * i + 1]));
    return x;
}

std::vector<cf> boxcar_mf(const std::vector<cf> &x) {
    std::vector<cf> y(x.size());
    std::complex<double> acc(0.0, 0.0);
    for (size_t n = 0; n < x.size(); ++n) {
        acc += std::complex<double>(x[n]);
        if (n >= static_cast<size_t>(SYM_LEN)) acc -= std::complex<double>(x[n - SYM_LEN]);
        y[n] = cf(static_cast<float>(acc.real() / SYM_LEN), static_cast<float>(acc.imag() / SYM_LEN));
    }
    return y;
}

StreamDecoder::StreamDecoder(ModKind mod, double sample_rate_hz, size_t max_frame_samples)
    : mod_(mod), fs_(sample_rate_hz), max_frame_samples_(max_frame_samples) {}

std::vector<Frame> StreamDecoder::feed(const int16_t *iq, size_t n_complex) {
    std::vector<Frame> out;
    for (size_t i = 0; i < n_complex; ++i) buf_.emplace_back(static_cast<float>(iq[2 * i]), static_cast<float>(iq[2 * i + 1]));

    const size_t span = (PREAMBLE_SYMBOLS - 1) * SYM_LEN;
    const std::vector<cf> y = boxcar_mf(buf_);
    const size_t n = y.size();

    size_t t = std::max<size_t>(scan_pos_, SYM_LEN - 1);
    while (true) {
        // первое положение, где корреляция выше порога
        size_t tc = 0;
        bool found = false;
        for (; t + span < n; ++t) {
            if (sync_metric(y, t, min_power) >= sync_threshold) {
                tc = t;
                found = true;
                break;
            }
        }
        if (!found) {
            scan_pos_ = t;
            break;
        }
        // не хватает отсчётов для поиска вершины пика
        if (tc + 2 * SYM_LEN + span >= n) {
            scan_pos_ = tc;
            break;
        }
        // вершина пика = точное начало кадра
        size_t best = tc;
        float best_m = -1.f;
        for (size_t u = tc; u <= tc + 2 * SYM_LEN; ++u) {
            const float m = sync_metric(y, u, min_power);
            if (m > best_m) {
                best_m = m;
                best = u;
            }
        }

        Frame f;
        size_t end_y = 0;
        const Status st = try_decode(y, best, mod_, fs_, max_frame_samples_, min_power, f, end_y);
        // кадр пришёл не целиком, ждём следующий буфер
        if (st == Status::Incomplete) {
            scan_pos_ = tc;
            break;
        }
        if (st == Status::Ok) {
            f.start_sample = base_ + best - (SYM_LEN - 1);
            f.end_sample = base_ + end_y + 1;
            out.push_back(std::move(f));
            ++ok_;
            t = end_y + 1;
        } else {
            ++bad_;
            t = best + span + 1;
        }
    }

    // выбрасываем из буфера лишнее
    const size_t keep_from = scan_pos_ >= static_cast<size_t>(SYM_LEN) ? scan_pos_ - SYM_LEN : 0;
    if (keep_from > 0) {
        buf_.erase(buf_.begin(), buf_.begin() + static_cast<long>(keep_from));
        base_ += keep_from;
        scan_pos_ -= keep_from;
    }
    return out;
}

}  // namespace sdr
