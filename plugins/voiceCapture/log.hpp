#include <iostream>
#include <optional>

#pragma once

#define LOG(s) std::cout << "[HADEAN] " << s << '\n'

template<typename T> std::ostream& operator<<(std::ostream& os, std::optional<T> const& opt) {
    return opt ? os << opt.value() : os << "<nullopt>";
}
