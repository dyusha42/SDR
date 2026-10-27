#include <SoapySDR/Device.h>
#include <SoapySDR/Formats.h>
#include <complex.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

int main(void) {
    printf("[INFO] Инициализация SDR устройства Adalm-Pluto...\n");

    // Задаем параметры подключения
    SoapySDRKwargs args = {};
    SoapySDRKwargs_set(&args, "driver", "plutosdr");
    SoapySDRKwargs_set(&args, "uri", "usb:");
    SoapySDRKwargs_set(&args, "direct", "1");
    SoapySDRKwargs_set(&args, "timestamp_every", "1920");
    SoapySDRKwargs_set(&args, "loopback", "0");

    SoapySDRDevice *sdr = SoapySDRDevice_make(&args);
    SoapySDRKwargs_clear(&args);

    if (sdr == NULL) {
        fprintf(stderr, "[ERROR] Ошибка подключения к SDR: %s\n", SoapySDRDevice_lastError());
        return EXIT_FAILURE;
    }

    // Настройка рабочих частот и частоты дискретизации
    const double sample_rate = 1e6;   // 1 МГц
    const double carrier_freq = 800e6; // 800 МГц

    SoapySDRDevice_setSampleRate(sdr, SOAPY_SDR_RX, 0, sample_rate);
    SoapySDRDevice_setFrequency(sdr, SOAPY_SDR_RX, 0, carrier_freq, NULL);

    SoapySDRDevice_setSampleRate(sdr, SOAPY_SDR_TX, 0, sample_rate);
    SoapySDRDevice_setFrequency(sdr, SOAPY_SDR_TX, 0, carrier_freq, NULL);

    // Усиление (Gain)
    size_t channel = 0;
    SoapySDRDevice_setGain(sdr, SOAPY_SDR_RX, channel, 30.0);
    SoapySDRDevice_setGain(sdr, SOAPY_SDR_TX, channel, -40.0);

    // Настройка потоков в формате комплексных 16-битных чисел CS16
    size_t channels[] = {0};
    SoapySDRStream *rxStream = SoapySDRDevice_setupStream(
        sdr, SOAPY_SDR_RX, SOAPY_SDR_CS16, channels, 1, NULL);
    SoapySDRStream *txStream = SoapySDRDevice_setupStream(
        sdr, SOAPY_SDR_TX, SOAPY_SDR_CS16, channels, 1, NULL);

    if (!rxStream || !txStream) {
        fprintf(stderr, "[ERROR] Ошибка создания потоков RX/TX\n");
        SoapySDRDevice_unmake(sdr);
        return EXIT_FAILURE;
    }

    size_t rx_mtu = SoapySDRDevice_getStreamMTU(sdr, rxStream);
    size_t tx_mtu = SoapySDRDevice_getStreamMTU(sdr, txStream);

    int16_t rx_buffer[2 * rx_mtu];
    int16_t tx_buffer[2 * tx_mtu];

    // Заполнение TX буфера тестовым сигналом
    for (size_t i = 0; i < 2 * tx_mtu; i += 2) {
        tx_buffer[i]     = 2000; // I-составляющая
        tx_buffer[i + 1] = 2000; // Q-составляющая
    }

    // Вставка служебных заголовков для привязки времени
    tx_buffer[0] = 0xffff;
    tx_buffer[1] = 0xffff;

    // Активация потоков
    SoapySDRDevice_activateStream(sdr, rxStream, 0, 0, 0);
    SoapySDRDevice_activateStream(sdr, txStream, 0, 0, 0);

    FILE *output_file = fopen("txdata.pcm", "wb");
    if (!output_file) {
        perror("[ERROR] Не удалось создать файл txdata.pcm");
        return EXIT_FAILURE;
    }

    const long timeoutUs = 400000;
    long long last_time = 0;
    const size_t total_buffers = 10;

    for (size_t read_idx = 0; read_idx < total_buffers; read_idx++) {
        void *rx_buffs[] = {rx_buffer};
        int flags = 0;
        long long timeNs = 0;

        int samples_read = SoapySDRDevice_readStream(
            sdr, rxStream, rx_buffs, rx_mtu, &flags, &timeNs, timeoutUs);

        if (samples_read < 0) {
            continue;
        }

        printf("Buffer: %zu - Samples: %d, Flags: %d, Time: %lld, TimeDiff: %lld\n",
               read_idx, samples_read, flags, timeNs, timeNs - last_time);

        fwrite(rx_buffer, sizeof(int16_t), 2 * rx_mtu, output_file);
        last_time = timeNs;

        // Планирование передачи через 4 мс в будущее
        long long tx_time = timeNs + (4 * 1000 * 1000);

        for (size_t i = 0; i < 8; i++) {
            uint8_t tx_byte = (tx_time >> (i * 8)) & 0xff;
            tx_buffer[2 + i] = tx_byte << 4;
        }

        // Отправка TX пакета на 3-ей итерации
        if (read_idx == 2) {
            printf("buffers_read: %zu\n", read_idx);
            void *tx_buffs[] = {tx_buffer};
            flags = SOAPY_SDR_HAS_TIME;

            int st = SoapySDRDevice_writeStream(
                sdr, txStream, (const void * const*)tx_buffs, tx_mtu, &flags, tx_time, timeoutUs);
            
            if ((size_t)st != tx_mtu) {
                printf("TX Failed: %d\n", st);
            }
        }
    }

    fclose(output_file);

    // Завершение работы
    SoapySDRDevice_deactivateStream(sdr, rxStream, 0, 0);
    SoapySDRDevice_deactivateStream(sdr, txStream, 0, 0);

    SoapySDRDevice_closeStream(sdr, rxStream);
    SoapySDRDevice_closeStream(sdr, txStream);

    SoapySDRDevice_unmake(sdr);
    printf("Тест успешно завершён!\n");

    return EXIT_SUCCESS;
}