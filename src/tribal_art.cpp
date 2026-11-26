#include "tribal_art.hpp"

#include <array>
#include <cmath>
#include <functional>

namespace sdr {

namespace {

constexpr float kPi = 3.14159265f;
constexpr int kSteps = 26;

using Curve = std::array<Pt, 4>;

Pt bezier(const Curve &c, float t) {
    const float u = 1.f - t;
    const float a = u * u * u, b = 3 * u * u * t, d = 3 * u * t * t, e = t * t * t;
    return {a * c[0].x + b * c[1].x + d * c[2].x + e * c[3].x, a * c[0].y + b * c[1].y + d * c[2].y + e * c[3].y};
}

Pt bezier_dir(const Curve &c, float t) {
    const float u = 1.f - t;
    const float a = 3 * u * u, b = 6 * u * t, d = 3 * t * t;
    Pt v{a * (c[1].x - c[0].x) + b * (c[2].x - c[1].x) + d * (c[3].x - c[2].x),
         a * (c[1].y - c[0].y) + b * (c[2].y - c[1].y) + d * (c[3].y - c[2].y)};
    const float len = std::hypot(v.x, v.y);
    return len > 1e-6f ? Pt{v.x / len, v.y / len} : Pt{1.f, 0.f};
}

// профиль толщины: быстро нарастает у основания и сходит в острие
float width_at(float t) { return std::pow(1.f - t, 1.1f) * std::sqrt(std::fmin(1.f, t * 6.f)); }

Blade make_blade(const Curve &c, float base_w, float shade) {
    Blade b;
    b.shade = shade;
    for (int i = 0; i <= kSteps; ++i) {
        const float t = static_cast<float>(i) / kSteps;
        const Pt p = bezier(c, t), d = bezier_dir(c, t);
        const float w = base_w * width_at(t);
        b.left.push_back({p.x - d.y * w, p.y + d.x * w});
        b.right.push_back({p.x + d.y * w, p.y - d.x * w});
    }
    return b;
}

// ответвление-шип от точки t на кривой, повёрнутое на angle
Curve branch_from(const Curve &c, float t, float angle, float length) {
    const Pt p = bezier(c, t), d = bezier_dir(c, t);
    const float ca = std::cos(angle), sa = std::sin(angle);
    const Pt v{d.x * ca - d.y * sa, d.x * sa + d.y * ca};
    return {p, Pt{p.x + v.x * length * 0.35f, p.y + v.y * length * 0.35f},
            Pt{p.x + v.x * length * 0.75f + d.x * length * 0.1f, p.y + v.y * length * 0.75f + d.y * length * 0.1f},
            Pt{p.x + v.x * length, p.y + v.y * length}};
}

// кривая в проектных координатах -> пиксели (с отражением)
Curve place(const Curve &c, float ox, float oy, float sx, float sy, float scale, bool swap_xy) {
    Curve r;
    for (int i = 0; i < 4; ++i) {
        const float x = swap_xy ? c[i].y : c[i].x, y = swap_xy ? c[i].x : c[i].y;
        r[i] = {ox + sx * x * scale, oy + sy * y * scale};
    }
    return r;
}

struct Spec {
    Curve curve;
    float width, shade;
};

}  // namespace

std::vector<Blade> build_tribal_art(float w, float h) {
    std::vector<Blade> out;
    const float m = std::fmin(w, h);

    // угловой узор (для левого верхнего угла): два длинных лезвия вдоль кромок и два изогнутых когтя
    const std::vector<Spec> corner = {
        {{Pt{0.00f, 0.05f}, Pt{0.25f, -0.03f}, Pt{0.48f, 0.13f}, Pt{0.78f, 0.09f}}, 0.060f, 1.00f},
        {{Pt{0.03f, 0.03f}, Pt{0.24f, 0.05f}, Pt{0.38f, 0.22f}, Pt{0.30f, 0.50f}}, 0.055f, 0.90f},
        {{Pt{0.10f, 0.00f}, Pt{0.30f, 0.05f}, Pt{0.52f, 0.02f}, Pt{0.66f, -0.06f}}, 0.022f, 0.60f},
    };
    const float cscale = 0.50f * m;
    for (int sx : {1, -1}) {
        for (int sy : {1, -1}) {
            const float ox = sx > 0 ? 0.f : w, oy = sy > 0 ? 0.f : h;
            const float fx = static_cast<float>(sx), fy = static_cast<float>(sy);
            for (bool swap_xy : {false, true}) {
                for (const Spec &sp : corner)
                    out.push_back(make_blade(place(sp.curve, ox, oy, fx, fy, cscale, swap_xy), sp.width * cscale, sp.shade));

                // шипы на длинном лезвии
                const Curve main = place(corner[0].curve, ox, oy, fx, fy, cscale, swap_xy);
                const float turn = ((sx * sy > 0) != swap_xy) ? 1.f : -1.f;
                for (float t : {0.30f, 0.55f, 0.78f})
                    out.push_back(make_blade(branch_from(main, t, turn * 0.95f, (0.20f - 0.05f * t) * cscale), 0.030f * cscale, 0.75f));
            }
        }
    }

    // гребни по центру верхней и нижней кромок: крылья и кинжал по оси
    const float cs = 0.42f * m;
    const std::vector<Spec> crest = {
        {{Pt{0.00f, 0.00f}, Pt{0.15f, 0.06f}, Pt{0.38f, -0.01f}, Pt{0.62f, 0.14f}}, 0.050f, 0.90f},
        {{Pt{0.00f, 0.07f}, Pt{0.12f, 0.15f}, Pt{0.28f, 0.12f}, Pt{0.42f, 0.30f}}, 0.036f, 0.70f},
    };
    for (int sy : {1, -1}) {
        const float oy = sy > 0 ? 0.f : h;
        const float fy = static_cast<float>(sy);
        for (int sx : {1, -1})
            for (const Spec &sp : crest)
                out.push_back(make_blade(place(sp.curve, w * 0.5f, oy, static_cast<float>(sx), fy, cs, false), sp.width * cs, sp.shade));
        const Curve dagger = {Pt{0.f, 0.f}, Pt{0.025f, 0.13f}, Pt{-0.025f, 0.26f}, Pt{0.f, 0.44f}};
        out.push_back(make_blade(place(dagger, w * 0.5f, oy, 1.f, fy, cs, false), 0.045f * cs, 0.80f));
    }

    // центральный знак: крылья и острия, виден сквозь полупрозрачные графики
    const float es = 0.55f * m;
    const std::vector<Spec> emblem = {
        {{Pt{0.00f, 0.00f}, Pt{-0.12f, -0.09f}, Pt{-0.30f, -0.07f}, Pt{-0.50f, 0.06f}}, 0.050f, 0.30f},
        {{Pt{0.00f, 0.03f}, Pt{-0.10f, 0.10f}, Pt{-0.24f, 0.14f}, Pt{-0.36f, 0.28f}}, 0.040f, 0.26f},
        {{Pt{0.00f, 0.00f}, Pt{-0.06f, -0.14f}, Pt{-0.16f, -0.22f}, Pt{-0.20f, -0.36f}}, 0.030f, 0.24f},
    };
    for (int sx : {1, -1})
        for (const Spec &sp : emblem)
            out.push_back(make_blade(place(sp.curve, w * 0.5f, h * 0.5f, static_cast<float>(-sx), 1.f, es, false), sp.width * es, sp.shade));
    for (int sy : {1, -1}) {
        const Curve spike = {Pt{0.f, 0.f}, Pt{0.02f, 0.10f}, Pt{-0.02f, 0.22f}, Pt{0.f, 0.40f}};
        out.push_back(make_blade(place(spike, w * 0.5f, h * 0.5f, 1.f, static_cast<float>(sy), es, false), 0.045f * es, 0.30f));
    }
    return out;
}

}  // namespace sdr
