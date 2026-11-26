// лаба 4: приёмник (согласованный фильтр, поиск кадра, коррекция, демодуляция)
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <stdexcept>
#include <thread>

#include "cli.hpp"
#include "common.hpp"
#include "iq_io.hpp"
#include "live_data.hpp"
#include "rx_dsp.hpp"
#include "runtime.hpp"

#ifdef HAVE_SDR
#include "sdr_device.hpp"
#endif

#ifdef HAVE_GUI
#include "gui.hpp"
#endif

using namespace sdr;

static void usage() {
    std::printf(
        "Использование: lab4_rx [параметры]\n"
        "  --mod bpsk|qpsk   модуляция передатчика                      (qpsk)\n"
        "  --file F.pcm      обработать запись вместо приёма с Pluto\n"
        "  --out F.pcm       куда сохранять приём с Pluto               (rxdata.pcm)\n"
        "  --symbols F.csv   точки созвездия                            (symbols.csv)\n"
        "  --frames F.csv    позиции найденных кадров                   (frames.csv)\n"
        "  --filtered F.pcm  сигнал после согласованного фильтра (сумма 10 отсчётов) (mf_out.pcm)\n"
        "  --gui             живое окно с графиками (нужна сборка с ImGui, только приём с Pluto)\n"
        "  --expect ТЕКСТ    проверить принятый текст (код возврата 0 = успех)\n"
        "  --rate Гц         частота дискретизации                      (1e6)\n"
        "  --uri URI         адрес Pluto, например usb:1.4.5            (usb:)\n"
        "  --rx-freq Гц      частота приёма                             (800e6)\n"
        "  --rx-gain дБ      усиление приёмника                         (30)\n"
        "  --agc             включить автоусиление приёмника (по умолчанию выключено)\n"
        "  --buffers N       сколько буферов принять, 0 = до Ctrl+C     (1000)\n"
        "  --verbose         подробный вывод\n");
}

static std::string printable(const std::string &s) {
    std::string r;
    for (unsigned char c : s) r += (c >= 32 && c < 127) ? static_cast<char>(c) : '?';
    return r;
}

struct Stats {
    size_t ok = 0, wrong = 0;
    double cfo_sum = 0, snr_sum = 0, amp_sum = 0;
    int csv_frames = 0;
};

// сигнал после согласованного фильтра в int16 (сумма 10 отсчётов, без деления)
static void save_filtered(const std::vector<int16_t> &iq, const std::string &path) {
    const std::vector<cf> y = boxcar_mf(to_complex(iq.data(), iq.size() / 2));
    std::vector<int16_t> out(2 * y.size());
    for (size_t i = 0; i < y.size(); ++i) {
        out[2 * i] = static_cast<int16_t>(std::lround(std::fmax(-32768.0f, std::fmin(32767.0f, y[i].real() * SYM_LEN))));
        out[2 * i + 1] = static_cast<int16_t>(std::lround(std::fmax(-32768.0f, std::fmin(32767.0f, y[i].imag() * SYM_LEN))));
    }
    write_pcm(path, out);
}

// вывод кадра и запись в csv
static void report(const Frame &f, size_t idx, const std::string &expect, bool has_expect, Stats &st, std::ofstream &sym_csv,
                   std::ofstream &frm_csv) {
    const bool match = !has_expect || f.text == expect;
    std::printf("[кадр %zu] отсчёт %llu | CFO = %+8.1f Гц | SNR = %5.1f дБ | корреляция = %.2f | CRC OK | \"%s\"%s\n", idx,
                static_cast<unsigned long long>(f.start_sample), f.cfo_hz, f.snr_db, f.sync_metric, printable(f.text).c_str(),
                match ? "" : "   <-- НЕ СОВПАДАЕТ с ожидаемым");
    if (match) ++st.ok; else ++st.wrong;
    st.cfo_sum += f.cfo_hz;
    st.snr_sum += f.snr_db;
    st.amp_sum += f.amplitude;
    frm_csv << idx << "," << f.start_sample << "," << f.end_sample << "," << f.cfo_hz << "," << f.snr_db << "\n";
    if (st.csv_frames < 20) {
        for (size_t k = 0; k < f.raw_symbols.size(); ++k) sym_csv << "raw," << idx << "," << k << "," << f.raw_symbols[k].real() << "," << f.raw_symbols[k].imag() << "\n";
        for (size_t k = 0; k < f.corr_symbols.size(); ++k) sym_csv << "corr," << idx << "," << k << "," << f.corr_symbols[k].real() << "," << f.corr_symbols[k].imag() << "\n";
        ++st.csv_frames;
    }
}

int main(int argc, char **argv) {
    Args args(argc, argv, {"help", "verbose", "gui", "agc"});
    if (!args.ok()) {
        std::printf("Ошибка: %s\n", args.error().c_str());
        return 1;
    }
    std::string bad = args.first_unknown({"mod", "file", "out", "symbols", "frames", "filtered", "gui", "expect", "rate", "uri", "rx-freq", "rx-gain", "buffers", "verbose", "agc", "help"});
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
    const double rate = args.get_double("rate", DEFAULT_RATE);
    const bool has_expect = args.has("expect");
    const std::string expect = args.get("expect");
    [[maybe_unused]] const bool verbose = args.has("verbose");
    const std::string sym_name = args.get("symbols", "symbols.csv");
    const std::string frm_name = args.get("frames", "frames.csv");
    const std::string flt_name = args.get("filtered", "mf_out.pcm");

    try {
        StreamDecoder dec(mod, rate);
        Stats st;
        std::ofstream sym_csv(sym_name), frm_csv(frm_name);
        sym_csv << "kind,frame,k,I,Q\n";
        frm_csv << "frame,start_sample,end_sample,cfo_hz,snr_db\n";
        size_t frame_idx = 0;
        size_t n_samples = 0;

        if (args.has("file")) {
            // обработка записи из файла, буфер за буфером
            std::vector<int16_t> iq = read_pcm(args.get("file"));
            const size_t n = iq.size() / 2;
            std::printf("Файл %s: %zu отсчётов (%.2f мс при %.3f МГц), модуляция %s\n", args.get("file").c_str(), n,
                        1e3 * static_cast<double>(n) / rate, rate / 1e6, to_string(mod));
            for (size_t pos = 0; pos < n; pos += DEFAULT_MTU) {
                size_t len = std::min(DEFAULT_MTU, n - pos);
                for (const Frame &f : dec.feed(iq.data() + 2 * pos, len)) report(f, ++frame_idx, expect, has_expect, st, sym_csv, frm_csv);
            }
            n_samples = n;
            save_filtered(iq, flt_name);
        } else {
#ifndef HAVE_SDR
            std::printf("Сборка без SoapySDR. Используйте --file или установите SoapySDR и пересоберите проект.\n");
            return 1;
#else
            install_sigint_handler();

            // настройка устройства
            SdrConfig cfg;
            cfg.uri = args.get("uri", "usb:");
            cfg.rate = rate;
            cfg.rx_hz = args.get_double("rx-freq", 800e6);
            cfg.rx_db = args.get_double("rx-gain", 30.0);
            cfg.agc = args.has("agc");
            cfg.tx_db = 0.0;
            const long max_buffers = args.get_long("buffers", 1000);
            const std::string out_name = args.get("out", "rxdata.pcm");

            SdrDevice dev(cfg);
            std::printf("Pluto %s: RX %.3f МГц, rate %.3f МГц, MTU = %zu. Модуляция %s. Остановка - Ctrl+C.\n", cfg.uri.c_str(),
                        cfg.rx_hz / 1e6, cfg.rate / 1e6, dev.rx_len(), to_string(mod));

            FILE *rec = std::fopen(out_name.c_str(), "wb");
            if (!rec) throw std::runtime_error("не удалось открыть " + out_name + " для записи");

            // данные для окна (только с --gui)
            LiveData live;
            const bool use_gui = args.has("gui");
#ifndef HAVE_GUI
            if (use_gui) {
                std::printf("Сборка без окна ImGui. Установите SDL2 и скачайте third_party/imgui, third_party/implot (см. README), затем пересоберите.\n");
                std::fclose(rec);
                return 1;
            }
#endif

            // основной цикл: читаем буфер, пишем в файл, ищем кадры
            std::vector<int16_t> buf(2 * dev.rx_len());
            long long prev_ts = 0;
            long blocks_done = 0, errors = 0;
            auto capture = [&]() {
                while (!g_stop && (max_buffers == 0 || blocks_done < max_buffers)) {
                    long long time_ns = prev_ts;
                    int flags = 0;
                    int sr = dev.read(buf.data(), time_ns, flags);
                    if (sr < 0) {
                        if (verbose) std::printf("readStream: код %d\n", sr);
                        if (++errors > 200) {
                            std::printf("Слишком много ошибок чтения. Остановка.\n");
                            break;
                        }
                        continue;
                    }
                    prev_ts = time_ns;
                    ++blocks_done;
                    std::fwrite(buf.data(), 2 * sizeof(int16_t), static_cast<size_t>(sr), rec);
                    n_samples += static_cast<size_t>(sr);
                    if (use_gui) live.push_samples(buf.data(), static_cast<size_t>(sr));
                    for (const Frame &f : dec.feed(buf.data(), static_cast<size_t>(sr))) {
                        report(f, ++frame_idx, expect, has_expect, st, sym_csv, frm_csv);
                        if (use_gui) live.push_frame(f);
                    }
                    if (use_gui) live.set_counts(dec.frames_ok(), dec.frames_bad());
                    if (verbose && blocks_done % 100 == 0) std::printf("... буферов: %ld, кадров: %zu\n", blocks_done, dec.frames_ok());
                }
            };

#ifdef HAVE_GUI
            if (use_gui) {
                // приём в отдельном потоке, окно в главном
                std::thread worker(capture);
                std::string kind = to_string(mod);
                for (char &ch : kind) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
                char info[160];
                std::snprintf(info, sizeof info, "%s  |  %.3f MHz  |  %.3f Msps  |  %s", kind.c_str(), cfg.rx_hz / 1e6, cfg.rate / 1e6, cfg.uri.c_str());
                run_gui(live, info);
                g_stop = 1;
                worker.join();
            } else {
                capture();
            }
#else
            capture();
#endif
            std::fclose(rec);
            std::printf("Принято %ld буферов, запись сохранена в %s\n", blocks_done, out_name.c_str());
            save_filtered(read_pcm(out_name), flt_name);
#endif
        }

        // итоги
        std::printf("---\nОтсчётов обработано: %zu. Кадров принято верно: %zu, ошибок CRC / ложных срабатываний: %zu\n", n_samples,
                    dec.frames_ok(), dec.frames_bad());
        const size_t found = st.ok + st.wrong;
        if (found > 0) {
            const double amp = st.amp_sum / static_cast<double>(found);
            std::printf("Средняя расстройка частоты: %+.1f Гц, средний SNR: %.1f дБ, амплитуда сигнала: %.1f (предел АЦП 2047)\n",
                        st.cfo_sum / static_cast<double>(found), st.snr_sum / static_cast<double>(found), amp);
            std::printf("Точки созвездия: %s, позиции кадров: %s, после фильтра: %s\n", sym_name.c_str(), frm_name.c_str(), flt_name.c_str());
            if (amp > 1000) std::printf("ВНИМАНИЕ: сигнал близок к перегрузке АЦП, уменьшите --tx-gain.\n");
            if (amp < 5) std::printf("ВНИМАНИЕ: сигнал очень слабый, увеличьте --tx-gain или сблизьте платы.\n");
        } else {
            std::printf("Кадры не найдены. Проверьте --mod, частоты, усиление и что передатчик запущен.\n");
        }

        if (st.ok == 0) return 1;
        if (has_expect && st.wrong > 0) return 1;
        return 0;
    } catch (const std::exception &e) {
        std::printf("Ошибка: %s\n", e.what());
        return 1;
    }
}
