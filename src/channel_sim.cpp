// имитатор канала: tx_burst.pcm -> rxdata.pcm (шум, расстройка частоты, задержка)
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <random>

#include "cli.hpp"
#include "common.hpp"
#include "iq_io.hpp"

using namespace sdr;

static void usage() {
    std::printf(
        "Использование: channel_sim --in tx_burst.pcm --out rxdata.pcm [параметры]\n"
        "  --buffers N   число буферов по 1920 отсчётов     (100)\n"
        "  --every K     кадр на каждом K-м буфере           (10)\n"
        "  --cfo Гц      расстройка частоты                  (8000)\n"
        "  --snr дБ      сигнал/шум                          (15)\n"
        "  --delay N     задержка кадра в отсчётах           (357)\n"
        "  --phase рад   начальная фаза                      (0.7)\n"
        "  --amp A       амплитуда сигнала в отсчётах АЦП    (20)\n"
        "  --dc D        постоянная составляющая             (0)\n"
        "  --lead N      буферов с мусором в начале записи   (0)\n"
        "  --rate Гц     частота дискретизации               (1e6)\n"
        "  --seed N      seed генератора шума                (1)\n");
}

int main(int argc, char **argv) {
    Args args(argc, argv, {"help"});
    if (!args.ok() || args.has("help") || !args.has("in") || !args.has("out")) {
        if (!args.ok()) std::printf("Ошибка: %s\n", args.error().c_str());
        usage();
        return args.has("help") ? 0 : 1;
    }
    std::string bad = args.first_unknown({"in", "out", "buffers", "every", "cfo", "snr", "delay", "phase", "amp", "dc", "lead", "rate", "seed", "help"});
    if (!bad.empty()) {
        std::printf("Неизвестный параметр --%s\n", bad.c_str());
        usage();
        return 1;
    }

    try {
        std::vector<int16_t> tx = read_pcm(args.get("in"));
        const size_t mtu = tx.size() / 2;
        const long buffers = args.get_long("buffers", 100);
        const long every = std::max(1L, args.get_long("every", 10));
        const double cfo = args.get_double("cfo", 8000.0);
        const double snr_db = args.get_double("snr", 15.0);
        const long delay = args.get_long("delay", 357);
        const double phase0 = args.get_double("phase", 0.7);
        const double amp = args.get_double("amp", 20.0);
        const double dc = args.get_double("dc", 0.0);
        const long lead = args.get_long("lead", 0);
        const double fs = args.get_double("rate", DEFAULT_RATE);
        std::mt19937 rng(static_cast<unsigned>(args.get_long("seed", 1)));

        const size_t total = static_cast<size_t>(buffers) * mtu;
        std::vector<double> re(total, 0.0), im(total, 0.0);

        // мощность сигнала по ненулевым отсчётам
        const double scale = amp / static_cast<double>(SYMBOL_AMP);
        double psig = 0.0;
        size_t cnt = 0;
        for (size_t i = 0; i < mtu; ++i) {
            double a = tx[2 * i] * scale, b = tx[2 * i + 1] * scale;
            if (a != 0.0 || b != 0.0) {
                psig += a * a + b * b;
                ++cnt;
            }
        }
        psig = cnt ? psig / static_cast<double>(cnt) : 1.0;

        // вставляем кадры
        size_t frames = 0;
        for (long b = every; b < buffers; b += every) {
            size_t start = static_cast<size_t>(b) * mtu + static_cast<size_t>(delay);
            for (size_t i = 0; i < mtu && start + i < total; ++i) {
                re[start + i] += tx[2 * i] * scale;
                im[start + i] += tx[2 * i + 1] * scale;
            }
            ++frames;
        }

        // расстройка частоты и фаза
        const double two_pi = 6.283185307179586;
        for (size_t n = 0; n < total; ++n) {
            double ph = two_pi * cfo * static_cast<double>(n) / fs + phase0;
            double c = std::cos(ph), s = std::sin(ph);
            double a = re[n], b = im[n];
            re[n] = a * c - b * s;
            im[n] = a * s + b * c;
        }

        // шум и квантование под 12-битный АЦП
        const double snr_lin = std::pow(10.0, snr_db / 10.0);
        const double sigma = std::sqrt(psig / (2.0 * snr_lin));
        std::normal_distribution<double> nd(0.0, sigma);
        std::vector<int16_t> out(2 * total);
        for (size_t n = 0; n < total; ++n) {
            double a = re[n] + nd(rng) + dc;
            double b = im[n] + nd(rng) + dc;
            a = std::fmax(-2047.0, std::fmin(2047.0, std::round(a)));
            b = std::fmax(-2047.0, std::fmin(2047.0, std::round(b)));
            out[2 * n] = static_cast<int16_t>(a);
            out[2 * n + 1] = static_cast<int16_t>(b);
        }

        // мусор в начале записи (как при старте Pluto)
        std::uniform_real_distribution<double> ud(-1500.0, 1500.0);
        for (size_t n = 0; n < static_cast<size_t>(lead) * mtu && n < total; ++n) {
            out[2 * n] = static_cast<int16_t>(ud(rng));
            out[2 * n + 1] = static_cast<int16_t>(ud(rng));
        }

        write_pcm(args.get("out"), out);
        std::printf("Записано %zu отсчётов (%ld буферов), кадров: %zu, CFO=%.0f Гц, SNR=%.1f дБ, амплитуда=%.1f\n", total, buffers, frames, cfo,
                    snr_db, amp);
    } catch (const std::exception &e) {
        std::printf("Ошибка: %s\n", e.what());
        return 1;
    }
    return 0;
}
