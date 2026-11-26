// лаба 3: передатчик (биты -> модуляция -> апсемплинг -> свёртка -> TX)
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <stdexcept>

#include "cli.hpp"
#include "common.hpp"
#include "frame.hpp"
#include "iq_io.hpp"
#include "runtime.hpp"
#include "tx_dsp.hpp"

#ifdef HAVE_SDR
#include "sdr_device.hpp"
#endif

using namespace sdr;

static void usage() {
    std::printf(
        "Использование: lab3_tx [параметры]\n"
        "  --mod bpsk|qpsk   модуляция                          (qpsk)\n"
        "  --msg ТЕКСТ       сообщение                          (sss)\n"
        "  --fill            повторять сообщение по кругу, пока не заполнится буфер\n"
        "  --dump DIR        сохранить этапы цепочки в DIR (для графиков)\n"
        "  --uri URI         адрес Pluto, например usb:3.4.5     (usb:)\n"
        "  --tx-freq Гц      частота передачи                   (800e6)\n"
        "  --rx-freq Гц      частота приёма                     (750e6)\n"
        "  --rate Гц         частота дискретизации              (1e6)\n"
        "  --tx-gain дБ      усиление передатчика, максимум 89  (60)\n"
        "  --rx-gain дБ      усиление приёмника                 (30)\n"
        "  --agc             включить автоусиление приёмника (по умолчанию выключено)\n"
        "  --every N         кадр на каждом N-м буфере          (10)\n"
        "  --buffers N       остановка через N буферов, 0 = Ctrl+C (0)\n");
}

// сохранение промежуточных результатов цепочки
static void dump_stages(const TxBurst &b, const std::string &dir, size_t mtu) {
    std::filesystem::create_directories(dir);
    {
        std::ofstream f(dir + "/bits.txt");
        for (uint8_t v : b.bits) f << static_cast<int>(v);
        f << "\n";
    }
    {
        std::ofstream f(dir + "/symbols.csv");
        f << "I,Q\n";
        for (size_t i = 0; i < b.n_syms; ++i) f << b.symbols[2 * i] << "," << b.symbols[2 * i + 1] << "\n";
    }
    write_pcm(dir + "/upsampled.pcm", b.upsampled);
    write_pcm(dir + "/shaped.pcm", b.shaped);
    write_pcm(dir + "/tx_burst.pcm", b.buffer);
    std::ofstream f(dir + "/info.txt");
    f << "mod=" << to_string(b.mod) << "\n";
    f << "sym_len=" << SYM_LEN << "\n";
    f << "preamble_symbols=" << PREAMBLE_SYMBOLS << "\n";
    f << "n_syms=" << b.n_syms << "\n";
    f << "guard_samples=" << TX_GUARD_SAMPLES << "\n";
    f << "mtu=" << mtu << "\n";
    f << "message=" << b.message << "\n";
}

int main(int argc, char **argv) {
    Args args(argc, argv, {"help", "fill", "agc"});
    if (!args.ok()) {
        std::printf("Ошибка: %s\n", args.error().c_str());
        return 1;
    }
    std::string bad = args.first_unknown({"mod", "msg", "fill", "dump", "uri", "tx-freq", "rx-freq", "rate", "tx-gain", "rx-gain", "every", "buffers", "agc", "help"});
    if (args.has("help") || !bad.empty()) {
        if (!bad.empty()) std::printf("Неизвестный параметр --%s\n\n", bad.c_str());
        usage();
        return bad.empty() ? 0 : 1;
    }

    ModKind mod;
    if (!parse_mod_kind(args.get("mod", "qpsk"), mod)) {
        std::printf("Ошибка: --mod должен быть bpsk или qpsk\n");
        return 1;
    }
    const std::string msg = args.get("msg", "sss");
    const bool fill = args.has("fill");

    try {
        // режим без SDR
        if (args.has("dump")) {
            TxBurst b = build_tx_burst(msg, mod, DEFAULT_MTU, fill);
            dump_stages(b, args.get("dump"), DEFAULT_MTU);
            std::printf("Модуляция: %s, сообщение: \"%s\" (%zu байт)\n", to_string(mod), b.message.c_str(), b.message.size());
            std::printf("Бит в кадре: %zu, символов (с преамбулой): %zu, занято отсчётов: %zu из %zu\n", b.bits.size(), b.n_syms,
                        b.used_samples, DEFAULT_MTU);
            std::printf("Этапы сохранены в %s/\n", args.get("dump").c_str());
            return 0;
        }

#ifndef HAVE_SDR
        std::printf("Сборка без SoapySDR. Используйте --dump DIR или установите SoapySDR и пересоберите проект.\n");
        return 1;
#else
        install_sigint_handler();

        // настройка устройства
        SdrConfig cfg;
        cfg.uri = args.get("uri", "usb:");
        cfg.rate = args.get_double("rate", DEFAULT_RATE);
        cfg.tx_hz = args.get_double("tx-freq", 800e6);
        cfg.rx_hz = args.get_double("rx-freq", 750e6);
        cfg.tx_db = args.get_double("tx-gain", 60.0);
        cfg.rx_db = args.get_double("rx-gain", 30.0);
        cfg.agc = args.has("agc");
        const long every = std::max(2L, args.get_long("every", 10));
        const long max_buffers = args.get_long("buffers", 0);

        SdrDevice dev(cfg);
        std::printf("Pluto %s: TX %.3f МГц, RX %.3f МГц, rate %.3f МГц, MTU TX/RX = %zu/%zu\n", cfg.uri.c_str(), cfg.tx_hz / 1e6,
                    cfg.rx_hz / 1e6, cfg.rate / 1e6, dev.tx_len(), dev.rx_len());

        // буфер передачи
        TxBurst burst = build_tx_burst(msg, mod, dev.tx_len(), fill);
        std::printf("Передаём %s-кадр \"%s\" (%zu символов) каждый %ld-й буфер. Остановка - Ctrl+C.\n", to_string(mod), burst.message.c_str(),
                    burst.n_syms, every);

        // основной цикл: читаем буфер, на каждом N-м передаём кадр
        std::vector<int16_t> rx(2 * dev.rx_len());
        long long prev_ts = 0;
        long blocks_done = 0, sent = 0, errors = 0;
        while (!g_stop && (max_buffers == 0 || blocks_done < max_buffers)) {
            long long time_ns = prev_ts;
            int flags = 0;
            int sr = dev.read(rx.data(), time_ns, flags);
            if (sr < 0) {
                if (++errors > 100) {
                    std::printf("Слишком много ошибок чтения (%d). Остановка.\n", sr);
                    return 1;
                }
                continue;
            }
            prev_ts = time_ns;
            ++blocks_done;
            if (blocks_done % every == 0) {
                int st = dev.write_at(burst.buffer.data(), dev.tx_len(), time_ns + TX_ADVANCE_NS);
                if (st != static_cast<int>(dev.tx_len())) {
                    std::printf("TX ошибка: %d\n", st);
                } else {
                    ++sent;
                    if (sent <= 5 || sent % 50 == 0) std::printf("TX #%ld: буфер %ld, время передачи %lld нс\n", sent, blocks_done, time_ns + TX_ADVANCE_NS);
                }
            }
        }
        std::printf("Остановлено. Прочитано буферов: %ld, передано кадров: %ld\n", blocks_done, sent);
        return 0;
#endif
    } catch (const std::exception &e) {
        std::printf("Ошибка: %s\n", e.what());
        return 1;
    }
}
