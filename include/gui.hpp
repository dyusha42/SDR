// живое окно с графиками (SDL2 + ImGui + ImPlot), тёмно-красная тема
#pragma once

#include <string>

#include "live_data.hpp"

namespace sdr {

// работает в главном потоке, пока окно не закрыто или не взведён g_stop
// subtitle - строка под заголовком (модуляция, частота, адрес платы)
int run_gui(LiveData &live, const std::string &subtitle);

}  // namespace sdr
