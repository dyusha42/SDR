#include "cli.hpp"

#include <cstdlib>

Args::Args(int argc, char **argv, const std::set<std::string> &bool_flags) {
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a.rfind("--", 0) != 0) {
            pos_.push_back(a);
            continue;
        }
        std::string name = a.substr(2);
        std::string value;
        size_t eq = name.find('=');
        if (eq != std::string::npos) {
            value = name.substr(eq + 1);
            name = name.substr(0, eq);
        } else if (bool_flags.count(name)) {
            value = "1";
        } else if (i + 1 < argc) {
            value = argv[++i];
        } else {
            error_ = "для параметра --" + name + " не указано значение";
            return;
        }
        opts_[name] = value;
    }
}

std::string Args::get(const std::string &key, const std::string &def) const {
    auto it = opts_.find(key);
    return it == opts_.end() ? def : it->second;
}

double Args::get_double(const std::string &key, double def) const {
    auto it = opts_.find(key);
    if (it == opts_.end()) return def;
    return std::strtod(it->second.c_str(), nullptr);
}

long Args::get_long(const std::string &key, long def) const {
    auto it = opts_.find(key);
    if (it == opts_.end()) return def;
    return static_cast<long>(std::strtod(it->second.c_str(), nullptr));
}

std::string Args::first_unknown(const std::set<std::string> &known) const {
    for (const auto &kv : opts_)
        if (!known.count(kv.first)) return kv.first;
    return "";
}
