// разбор аргументов: --name value, --name=value, булевы флаги
#pragma once

#include <map>
#include <set>
#include <string>
#include <vector>

class Args {
public:
    Args(int argc, char **argv, const std::set<std::string> &bool_flags = {});

    bool has(const std::string &key) const { return opts_.count(key) > 0; }
    std::string get(const std::string &key, const std::string &def = "") const;
    double get_double(const std::string &key, double def) const;
    long get_long(const std::string &key, long def) const;
    const std::vector<std::string> &positional() const { return pos_; }

    // имя первого неизвестного параметра или пустая строка
    std::string first_unknown(const std::set<std::string> &known) const;
    bool ok() const { return error_.empty(); }
    const std::string &error() const { return error_; }

private:
    std::map<std::string, std::string> opts_;
    std::vector<std::string> pos_;
    std::string error_;
};
