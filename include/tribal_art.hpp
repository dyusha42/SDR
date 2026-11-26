// неотрайбл-орнамент для фона окна: острые изогнутые лезвия, зеркальные по обеим осям
#pragma once

#include <vector>

namespace sdr {

struct Pt {
    float x, y;
};

// лезвие: две кромки одинаковой длины, между ними заливка
struct Blade {
    std::vector<Pt> left, right;
    float shade;  // яркость 0..1
};

// орнамент для окна w x h пикселей
std::vector<Blade> build_tribal_art(float w, float h);

}  // namespace sdr
