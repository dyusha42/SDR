// лаба 2: приём/передача на двух Pluto по метке времени
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <stdexcept>

#include "cli.hpp"
#include "common.hpp"
#include "iq_io.hpp"
#include "runtime.hpp"
#include "tx_dsp.hpp"

#ifdef HAVE_SDR
#include "sdr_device.hpp"
#endif

using namespace sdr;

static void usage() {
    std::printf(
        "Использование: lab2_link <tx|rx> [параметры]\n"
        "  --uri URI        адрес Pluto, например usb:3.12.5       (usb:)\n"
        "  --f1 Гц          первая частота                         (800e6)\n"
        "  --f2 Гц          вторая частота                         (850e6)\n"
        "                   rx: приём f1, передача f2;  tx: приём f2, передача f1\n"
        "  --rate Гц        частота дискретизации                  (1e6)\n"
        "  --tx-gain дБ     усиление передатчика, максимум 89      (60)\n"
        "  --rx-gain дБ     усиление приёмника                     (30)\n"
        "  --every K        импульс на каждом K-м буфере           (2)\n"
        "  --buffers N      сколько буферов принять, 0 = до Ctrl+C (1500)\n"
        "  --out FILE       файл записи (rxdata.pcm для rx, txdata.pcm для tx)\n"
        "  --agc            включить автоусиление приёмника (по умолчанию выключено)\n"
        "  --verbose        строка про каждый буфер\n"
        "  --sim FILE       без SDR: создать пример записи\n");
}

int main(int argc, char **argv) {
    Args args(argc, argv, {"help", "verbose", "agc"});
    if (!args.ok()) {
        std::printf("Ошибка: %s\n", args.error().c_str());
        return 1;
    }
    std::string bad = args.first_unknown({"uri", "f1", "f2", "rate", "tx-gain", "rx-gain", "every", "buffers", "out", "verbose", "agc", "sim", "help"});
    if (args.has("help") || !bad.empty()) {
        if (!bad.empty()) std::printf("Неизвестный параметр --%s\n\n", bad.c_str());
        usage();
        return bad.empty() ? 0 : 1;
    }

    const double rate = args.get_double("rate", DEFAULT_RATE);
    const long every = std::max(1L, args.get_long("every", 2));
    const long max_buffers = args.get_long("buffers", 1500);
    const int16_t tone_amp = SYMBOL_AMP;
    const double tone_hz = 50e3;

    try {
        // пример записи без железа
        if (args.has("sim")) {
            const long buffers = max_buffers > 0 ? max_buffers : 1500;
            std::vector<int16_t> burst = make_tone_burst(DEFAULT_MTU, rate, tone_hz, 10);
            std::vector<int16_t> out(2 * DEFAULT_MTU * static_cast<size_t>(buffers), 0);
            std::mt19937 rng(1);
            std::normal_distribution<double> nd(0.0, 1.0);
            for (auto &v : out) v = static_cast<int16_t>(std::lround(nd(rng)));
            for (long b = every; b < buffers; b += every)
                for (size_t i = 0; i < 2 * DEFAULT_MTU; ++i) out[2 * DEFAULT_MTU * static_cast<size_t>(b) + i] += burst[i];
            write_pcm(args.get("sim"), out);
            std::printf("Создан пример записи %s: %ld буферов, импульс на каждом %ld-м\n", args.get("sim").c_str(), buffers, every);
            return 0;
        }

        if (args.positional().empty() || (args.positional()[0] != "tx" && args.positional()[0] != "rx")) {
            usage();
            return 1;
        }
        const bool as_tx = args.positional()[0] == "tx";

#ifndef HAVE_SDR
        (void)as_tx;
        (void)tone_amp;
        std::printf("Сборка без SoapySDR. Для проверки без железа: lab2_link --sim rxdata.pcm\n");
        return 1;
#else
        install_sigint_handler();

        // настройка устройства
        const double f1 = args.get_double("f1", 800e6), f2 = args.get_double("f2", 850e6);
        SdrConfig cfg;
        cfg.uri = args.get("uri", "usb:");
        cfg.rate = rate;
        cfg.rx_hz = as_tx ? f2 : f1;
        cfg.tx_hz = as_tx ? f1 : f2;
        cfg.tx_db = args.get_double("tx-gain", 60.0);
        cfg.rx_db = args.get_double("rx-gain", 30.0);
        cfg.agc = args.has("agc");
        const std::string out_name = args.get("out", as_tx ? "txdata.pcm" : "rxdata.pcm");
        const bool verbose = args.has("verbose");

        SdrDevice dev(cfg);
        std::printf("Режим %s, Pluto %s: RX %.3f МГц, TX %.3f МГц, rate %.3f МГц, MTU TX/RX = %zu/%zu\n", as_tx ? "TX" : "RX", cfg.uri.c_str(),
                    cfg.rx_hz / 1e6, cfg.tx_hz / 1e6, cfg.rate / 1e6, dev.tx_len(), dev.rx_len());

        // тестовый импульс и файл записи
        std::vector<int16_t> burst = make_tone_burst(dev.tx_len(), rate, tone_hz, tone_amp);
        FILE *file = std::fopen(out_name.c_str(), "wb");
        if (!file) throw std::runtime_error("не удалось открыть " + out_name + " для записи");

        // основной цикл
        std::vector<int16_t> rx(2 * dev.rx_len());
        long long prev_ts = 0;
        long blocks_done = 0, errors = 0, pulses = 0;
        std::printf("Старт. Файл записи: %s. Остановка - Ctrl+C.\n", out_name.c_str());
        while (!g_stop && (max_buffers == 0 || blocks_done < max_buffers)) {
            long long time_ns = prev_ts;
            int flags = 0;
            int sr = dev.read(rx.data(), time_ns, flags);
            if (sr < 0) {
                if (++errors > 100) {
                    std::printf("Слишком много ошибок чтения (%d). Остановка.\n", sr);
                    break;
                }
                continue;
            }
            if (verbose || blocks_done < 10 || blocks_done % 100 == 0)
                std::printf("блок %ld | отсчётов %d | flags %d | ts %lld нс | dt %lld нс\n", blocks_done, sr, flags, time_ns, time_ns - prev_ts);
            prev_ts = time_ns;
            std::fwrite(rx.data(), 2 * sizeof(int16_t), static_cast<size_t>(sr), file);

            // передача импульса по метке времени
            if (as_tx && blocks_done > 0 && blocks_done % every == 0) {
                int st = dev.write_at(burst.data(), dev.tx_len(), time_ns + TX_ADVANCE_NS);
                if (st != static_cast<int>(dev.tx_len())) std::printf("Ошибка передачи: %d\n", st);
                else if (++pulses <= 3 || pulses % 50 == 0 || verbose)
                    std::printf("импульс %ld: блок %ld, время передачи %lld нс\n", pulses, blocks_done, time_ns + TX_ADVANCE_NS);
            }
            ++blocks_done;
        }
        std::fclose(file);
        std::printf("test complete! Прочитано буферов: %ld, импульсов передано: %ld, файл: %s\n", blocks_done, pulses, out_name.c_str());
        return 0;
#endif
    } catch (const std::exception &e) {
        std::printf("Ошибка: %s\n", e.what());
        return 1;
    }
}
