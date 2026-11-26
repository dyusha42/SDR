#include "runtime.hpp"

volatile std::sig_atomic_t g_stop = 0;

static void on_sigint(int) { g_stop = 1; }

void install_sigint_handler() {
    std::signal(SIGINT, on_sigint);
    std::signal(SIGTERM, on_sigint);
}
