#include "sdr_device.hpp"

#include <SoapySDR/Device.h>
#include <SoapySDR/Formats.h>

#include <stdexcept>
#include <utility>

namespace sdr {

namespace {

// ошибка SoapySDR -> исключение
void must(int rc, const char *what) {
    if (rc != 0) throw std::runtime_error(std::string(what) + ": " + SoapySDRDevice_lastError());
}

}  // namespace

SdrDevice::SdrDevice(const SdrConfig &cfg) {
    // параметры драйвера таблицей
    const std::pair<const char *, std::string> driver_opts[] = {
        {"driver", "plutosdr"}, {"uri", cfg.uri}, {"direct", "1"}, {"timestamp_every", "1920"}, {"loopback", "0"}};
    SoapySDRKwargs kw = {};
    for (const auto &o : driver_opts) SoapySDRKwargs_set(&kw, o.first, o.second.c_str());
    dev_ = SoapySDRDevice_make(&kw);
    SoapySDRKwargs_clear(&kw);
    if (!dev_)
        throw std::runtime_error(std::string("SoapySDRDevice_make: ") + SoapySDRDevice_lastError() +
                                 "\nПроверьте URI (scripts/find_sdr.sh) и подключение Pluto.");

    try {
        // частота дискретизации и несущая для обоих направлений
        const std::pair<int, double> carriers[] = {{SOAPY_SDR_RX, cfg.rx_hz}, {SOAPY_SDR_TX, cfg.tx_hz}};
        for (const auto &c : carriers) {
            must(SoapySDRDevice_setSampleRate(dev_, c.first, 0, cfg.rate), "setSampleRate");
            must(SoapySDRDevice_setFrequency(dev_, c.first, 0, c.second, nullptr), "setFrequency");
        }

        // поток нужного направления (порядок как в проверенной раньше схеме: rx, усиления, tx)
        const size_t chan[] = {0};
        auto open_stream = [&](int dir) {
            SoapySDRStream *s = SoapySDRDevice_setupStream(dev_, dir, SOAPY_SDR_CS16, chan, 1, nullptr);
            if (!s) must(-1, "setupStream");
            return s;
        };
        rx_ = open_stream(SOAPY_SDR_RX);
        // режим усиления задаём до самого значения усиления
        must(SoapySDRDevice_setGainMode(dev_, SOAPY_SDR_RX, 0, cfg.agc), "setGainMode rx");
        must(SoapySDRDevice_setGain(dev_, SOAPY_SDR_RX, 0, cfg.rx_db), "setGain rx");
        must(SoapySDRDevice_setGain(dev_, SOAPY_SDR_TX, 0, cfg.tx_db), "setGain tx");
        tx_ = open_stream(SOAPY_SDR_TX);

        rx_len_ = SoapySDRDevice_getStreamMTU(dev_, rx_);
        tx_len_ = SoapySDRDevice_getStreamMTU(dev_, tx_);

        // запуск потоков
        for (SoapySDRStream *s : {rx_, tx_}) must(SoapySDRDevice_activateStream(dev_, s, 0, 0, 0), "activateStream");
    } catch (...) {
        if (rx_) SoapySDRDevice_closeStream(dev_, rx_);
        if (tx_) SoapySDRDevice_closeStream(dev_, tx_);
        SoapySDRDevice_unmake(dev_);
        dev_ = nullptr;
        throw;
    }
}

SdrDevice::~SdrDevice() {
    if (!dev_) return;
    for (SoapySDRStream *s : {rx_, tx_}) {
        SoapySDRDevice_deactivateStream(dev_, s, 0, 0);
        SoapySDRDevice_closeStream(dev_, s);
    }
    SoapySDRDevice_unmake(dev_);
}

int SdrDevice::read(int16_t *buf, long long &time_ns, int &flags, long timeout_us) {
    void *slots[] = {buf};
    flags = 0;
    return SoapySDRDevice_readStream(dev_, rx_, slots, rx_len_, &flags, &time_ns, timeout_us);
}

int SdrDevice::write_at(const int16_t *buf, size_t n, long long time_ns, long timeout_us) {
    const void *slots[] = {buf};
    int flags = SOAPY_SDR_HAS_TIME;
    return SoapySDRDevice_writeStream(dev_, tx_, slots, n, &flags, time_ns, timeout_us);
}

}  // namespace sdr
