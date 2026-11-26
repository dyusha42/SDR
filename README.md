# Производственная практика - SDR
## Золотухин Андрей Алексеевич

Цифровая радиосвязь на двух ADALM-PLUTO: от FM-приёмника в GNU Radio до передатчика и приёмника с модуляцией BPSK/QPSK,
написанных на C++ поверх SoapySDR. Приёмник сам находит кадр по преамбуле, оценивает расстройку частоты, демодулирует
и проверяет CRC.

## Структура проекта

```md
"SDR"
├── CMakeLists.txt          # сборка
├── grc/                    # схемы GNU Radio (лаба 1)
│   ├── fm_radio.grc
│   └── wifi.grc
├── include/ src/           # код на C++
│   ├── frame, tx_dsp       # формат кадра, генерация бит, передатчик
│   ├── rx_dsp              # приёмник
│   ├── live_data, gui      # живое окно в тёмно-красной теме (lab4_rx --gui)
│   ├── tribal_art          # геометрия неотрайбл-фона окна
│   ├── sdr_device          # работа с Pluto через SoapySDR
│   ├── lab2_link           # программа лабы 2
│   ├── lab3_tx             # программа лабы 3
│   └── lab4_rx             # программа лабы 4
├── scripts/                # графики на Python и поиск Pluto
├── third_party/            # imgui, implot (сабмодули, для окна)
├── docs/
│   ├── report1..4.md       # отчёты
│   └── images/             # скриншоты
├── .gitmodules
├── .gitignore
└── README.md
```

## Сборка и запуск проекта

### 1. Требования и зависимости
Нужны компилятор C++17, CMake, Python3 с numpy и matplotlib, а также библиотеки для работы с Adalm-Pluto
(libiio, libad9361-iio, SoapySDR и драйвер SoapyPlutoSDR).

Установка пакетов в Ubuntu/Debian:
```bash
sudo apt update
sudo apt install -y build-essential cmake git libusb-1.0-0-dev libxml2-dev bison flex libcdk5-dev \
    libaio-dev libavahi-common-dev libavahi-client-dev pkg-config python3-numpy python3-matplotlib \
    libsdl2-dev libgl-dev
```
`libsdl2-dev` и `libgl-dev` нужны только для живого окна.

### 2. Сборка библиотек для Pluto
Ключ `-DCMAKE_POLICY_VERSION_MINIMUM=3.5` нужен, если установлен CMake 4.x.
```bash
mkdir -p ~/sdr_libs && cd ~/sdr_libs

git clone --branch v0.24 https://github.com/TelecomDep/libiio.git
cd libiio && cmake -B build -DENABLE_IPV6=OFF -DCMAKE_POLICY_VERSION_MINIMUM=3.5
cmake --build build -j$(nproc) && sudo cmake --install build && sudo ldconfig && cd ..

git clone --branch v0.3 https://github.com/TelecomDep/libad9361-iio.git
cd libad9361-iio && cmake -B build -DCMAKE_POLICY_VERSION_MINIMUM=3.5
cmake --build build -j$(nproc) && sudo cmake --install build && sudo ldconfig && cd ..

git clone --branch soapy-sdr-0.8.1 https://github.com/TelecomDep/SoapySDR.git
cd SoapySDR && cmake -B build -DCMAKE_POLICY_VERSION_MINIMUM=3.5
cmake --build build -j$(nproc) && sudo cmake --install build && sudo ldconfig && cd ..

git clone --branch sdr_gadget_timestamping https://github.com/TelecomDep/SoapyPlutoSDR.git
cd SoapyPlutoSDR && cmake -B build -DCMAKE_POLICY_VERSION_MINIMUM=3.5
cmake --build build -j$(nproc) && sudo cmake --install build && sudo ldconfig && cd ..
```

### 3. Сабмодули для окна (ImGui и ImPlot)
```bash
git submodule update --init --recursive
```
Если сабмодулей нет, источники можно скачать вручную:
```bash
mkdir -p third_party && cd third_party
git clone --branch v1.92.9 https://github.com/ocornut/imgui.git
git clone --branch v0.17 https://github.com/epezent/implot.git
cd ..
```
Без них проект тоже соберётся, но без параметра `--gui`.

### 4. Сборка проекта
```bash
mkdir -p build && cd build
cmake ..
make -j$(nproc)
```
В выводе `cmake` должна быть строка «SoapySDR найден» и «Окно ImGui включено». В `build/` появятся программы `lab2_link`, `lab3_tx` и `lab4_rx`.

### 5. Поиск устройств
```bash
./scripts/find_sdr.sh
```
В выводе нужны строки `uri = usb:X.Y.Z`, эти адреса передаются в параметр `--uri`.

### 6. Запуск лабораторных
Все программы запускаются из `build/`. Ниже `usb:1.4.5` — приёмник, `usb:3.4.5` — передатчик (подставьте свои адреса).

**Лаба 2** (приёмник запускать первым, передатчик в течение пары секунд после него; каждая программа работает 1500 буферов, около 3 с):
```bash
./lab2_link rx --uri usb:1.4.5
./lab2_link tx --uri usb:3.4.5
```

**Лабы 3 и 4** (приёмник запускать первым, в двух терминалах; передатчик остановить по Ctrl+C после завершения приёмника):
```bash
./lab4_rx --mod qpsk --uri usb:1.4.5 --buffers 1000
./lab3_tx --mod qpsk --uri usb:3.4.5
```
Автоусиление приёмника (АРУ) по умолчанию выключено, усиление задаётся `--rx-gain`; `--agc` включает АРУ. `--mod` (`bpsk` или `qpsk`) должен совпадать у обеих программ. Текст ограничен размером буфера: до 16 символов для BPSK и до 36 для QPSK.
`--msg` задаёт свой текст (по умолчанию `sss`), `--fill` повторяет сообщение через пробел, пока не заполнится весь буфер.

**Живое окно** (тёмно-красная тема с неотрайбл-фоном: счётчики кадров, принятые отсчёты, сигнал после согласованного фильтра, созвездие до и после коррекции):
```bash
./lab4_rx --mod qpsk --uri usb:1.4.5 --buffers 0 --gui
```
Под строкой с последним сообщением находится панель управления. Режимы правых графиков: `Symbols` (по умолчанию: кольцо до коррекции и четыре точки после),
`Trajectory` (весь путь сигнала после фильтра), `Offset` (значения фильтра при сдвиге точки выборки 0..9 внутри символа: при 0 четыре точки, при 5 сетка 3×3) и `Live XY` (живые I/Q с антенны).
Флажки `Lines` (соединять точки), `Spin` (вращать), `Auto` (автоматически двигать сдвиг и фазу), ползунок `Offset` и кнопка `Reset` (вернуть исходный вид).
Окно закрывается крестиком или клавишей Esc, приём останавливается вместе с ним. После приёма в `build/` сохраняются `rxdata.pcm`, `mf_out.pcm` (после согласованного фильтра), `symbols.csv` и `frames.csv`.

**Графики** (из корня репозитория):
```bash
python3 scripts/plot_iq.py build/rxdata.pcm --abs
python3 scripts/plot_constellation.py build/rxdata.pcm
python3 scripts/plot_mf.py build/mf_out.pcm
python3 scripts/plot_mf_compare.py --start 1334000
./build/lab3_tx --mod qpsk --dump build/tx_dump && python3 scripts/plot_tx_stages.py build/tx_dump
```
`plot_iq.py` рисует I/Q принятой записи, `plot_constellation.py` — выбор символьных отсчётов и созвездие (нужны `frames.csv` и `symbols.csv`, которые создаёт `lab4_rx`),
`plot_mf.py` — сигнал после согласованного фильтра, `plot_mf_compare.py` — тот же участок до и после фильтра (`--start` берётся из вывода `lab4_rx`),
последняя команда сохраняет и показывает этапы цепочки передатчика.

## Практические работы
|№|Задание|Отчет|Дата сдачи|
|:-:|-|:-:|:-:|
|1|Построение радио-приёмника.|[docs/report1.md](docs/report1.md)|15.09.26|
|2|Прием-передача сэмплов на 2 SDR.|[docs/report2.md](docs/report2.md)|20.10.26|
|3|Передатчик: биты -> модуляция -> upsampling -> свёртка -> TX.|[docs/report3.md](docs/report3.md)| |
|4|Приёмник: согласованный фильтр, выбор символьных отсчётов, синхронизация, демодуляция.|[docs/report4.md](docs/report4.md)| |

## Занятия
|№|Дата занятия|Тема лекции|Тема практики|Что делал?|
|:-:|--|--|--|--|
|1|08.09.26|Основы структурной схемы цифровой радиосвязи.|Архитектура Adalm Pluto SDR. GNU Radio. Построение радио-приёмника|Собрал радиоприемник в GNU Radio, но он не заработал|
|2|15.09.26|Основы структурной схемы цифровой радиосвязи|GNU Radio. Построение радио-приёмника, радио-передатчика.|Ничего не менял, запустил радиоприемник и он заработал + ловил wifi сигнал|
|3|22.09.26|Введение в архитектуру SDR-устройств| Знакомство с библиотеками Soapy SDR, Libio для работы с Adalm Pluto SDR. Инициализация SDR-устройства. Работа с буфером: получение цифровых IQ-отсчетов.|Установил библиотеки + запустил код для приемо-передачи на одной SDR|
|4|29.09.26|Введение в архитектуру SDR-устройств| Знакомство с библиотеками Soapy SDR, Libio для работы с Adalm Pluto SDR. Инициализация SDR-устройства. Работа с буфером: получение цифровых IQ-отсчетов. Продолжение.|Начал разделять код на TX/RX, но пока успехом это не увенчалось|
|5|06.10.26|Введение в архитектуру SDR-устройств|Работа с буфером: получение цифровых IQ-отсчетов. Прием-передача сэмплов на 2 SDR. Продолжение.|Разделил код, но ничего не заработало :(|
|6|13.10.26|Введение в архитектуру SDR-устройств|Работа с буфером: получение цифровых IQ-отсчетов. Прием-передача сэмплов на 2 SDR. Свертка|Дописал синхронизацию по времени, вроде что-то начало передаваться|
|7|20.10.26| - |Работа с буфером: получение цифровых IQ-отсчетов. Прием-передача сэмплов на 2 SDR.|Доработал python файл вывода полученного сигнала и все заработало :)|
|8|27.10.26|Приёмник: фильтрация, синхронизация и демодуляция.|Передатчик: биты -> модуляция -> upsampling -> свертка -> TX. Начало приемника.|Начал писать светрку, пока вроде получается|
|9|03.11.26|Приёмник: фильтрация, синхронизация и демодуляция.|Приемник. Выбор символьных отсчётов и их визуализация после согласованного фильтра|Дописал свертку, все заработало, даже что-то передается|
|10|10.11.26| | | |
|11|17.11.26| | | |
|12|24.11.26| | | |
|13|01.12.26| | | |
|14|08.12.26| | | |
|15|15.12.26| | | |
|16|22.12.26| | | |
|17|29.12.26| | | |

<!-- | | | | | -->
