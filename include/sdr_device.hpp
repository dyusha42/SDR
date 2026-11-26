// обёртка над SoapySDR для PlutoSDR
#pragma once

#include <string>

#include "common.hpp"

struct SoapySDRDevice;
struct SoapySDRStream;

namespace sdr {

struct SdrConfig {
    std::string uri = "usb:";    // адрес Pluto, см. scripts/find_sdr.sh
    double rate = DEFAULT_RATE;  // Гц
    double rx_hz = 800e6;      // Гц
    double tx_hz = 750e6;      // Гц
    double rx_db = 30.0;       // дБ
    double tx_db = 60.0;       // дБ, 89 = максимум
    bool agc = false;          // автоусиление приёмника (АРУ); по умолчанию выключено, усиление фиксировано
};

class SdrDevice {
public:
    // бросает std::runtime_error с текстом ошибки SoapySDR
    explicit SdrDevice(const SdrConfig &cfg);
    ~SdrDevice();
    SdrDevice(const SdrDevice &) = delete;
    SdrDevice &operator=(const SdrDevice &) = delete;

    size_t rx_len() const { return rx_len_; }
    size_t tx_len() const { return tx_len_; }

    // число прочитанных отсчётов или код ошибки < 0
    int read(int16_t *buf, long long &time_ns, int &flags, long timeout_us = 400000);

    // передача n отсчётов на момент time_ns
    int write_at(const int16_t *buf, size_t n, long long time_ns, long timeout_us = 400000);

private:
    SoapySDRDevice *dev_ = nullptr;
    SoapySDRStream *rx_ = nullptr;
    SoapySDRStream *tx_ = nullptr;
    size_t rx_len_ = 0, tx_len_ = 0;
};

}  // namespace sdr
