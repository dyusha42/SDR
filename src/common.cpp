#include "common.hpp"

#include <algorithm>
#include <cctype>

namespace sdr {

bool parse_mod_kind(const std::string &s, ModKind &out) {
    std::string t = s;
    std::transform(t.begin(), t.end(), t.begin(), [](unsigned char c) { return std::tolower(c); });
    if (t == "bpsk") {
        out = ModKind::BPSK;
        return true;
    }
    if (t == "qpsk") {
        out = ModKind::QPSK;
        return true;
    }
    return false;
}

}  // namespace sdr
