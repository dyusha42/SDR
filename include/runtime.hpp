// остановка по Ctrl+C
#pragma once

#include <csignal>

extern volatile std::sig_atomic_t g_stop;
void install_sigint_handler();
