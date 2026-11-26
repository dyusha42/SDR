#include "gui.hpp"

#include <SDL.h>
#include <SDL_opengl.h>

#include <cfloat>
#include <cmath>
#include <complex>
#include <cstdio>
#include <string>
#include <vector>

#include "backends/imgui_impl_opengl3.h"
#include "backends/imgui_impl_sdl2.h"
#include "imgui.h"
#include "frame.hpp"
#include "implot.h"
#include "rx_dsp.hpp"
#include "runtime.hpp"
#include "tribal_art.hpp"

namespace sdr {

namespace {

// ---------- палитра ----------

ImVec4 rgb(int r, int g, int b, float a = 1.f) { return ImVec4(r / 255.f, g / 255.f, b / 255.f, a); }
ImU32 pack(const ImVec4 &c) { return ImGui::ColorConvertFloat4ToU32(c); }

const ImVec4 kText = rgb(236, 222, 218);
const ImVec4 kMuted = rgb(158, 122, 122);
const ImVec4 kCrimson = rgb(224, 48, 61);
const ImVec4 kDeep = rgb(110, 20, 30);
const ImVec4 kPanel = rgb(26, 9, 13, 0.82f);
const ImVec4 kLineI = rgb(255, 77, 90);
const ImVec4 kLineQ = rgb(245, 196, 150);
const ImVec4 kDotRaw = rgb(176, 64, 74, 0.85f);
const ImVec4 kDotFix = rgb(255, 92, 102);
const ImVec4 kWarn = rgb(255, 150, 60);

// ---------- темы ImGui и ImPlot ----------

void apply_theme() {
    ImGuiStyle &st = ImGui::GetStyle();
    st.WindowRounding = 0.f;
    st.FrameRounding = 5.f;
    st.ChildRounding = 7.f;
    st.WindowBorderSize = 0.f;
    st.FrameBorderSize = 1.f;
    st.ItemSpacing = ImVec2(10.f, 8.f);
    st.WindowPadding = ImVec2(18.f, 14.f);

    ImVec4 *c = st.Colors;
    c[ImGuiCol_Text] = kText;
    c[ImGuiCol_TextDisabled] = kMuted;
    c[ImGuiCol_WindowBg] = rgb(11, 5, 7, 0.f);
    c[ImGuiCol_ChildBg] = kPanel;
    c[ImGuiCol_Border] = rgb(120, 22, 32, 0.8f);
    c[ImGuiCol_FrameBg] = rgb(40, 12, 17, 0.85f);
    c[ImGuiCol_FrameBgHovered] = rgb(70, 16, 24, 0.9f);
    c[ImGuiCol_FrameBgActive] = rgb(95, 20, 30, 0.9f);
    c[ImGuiCol_ScrollbarBg] = rgb(14, 6, 8, 0.6f);
    c[ImGuiCol_ScrollbarGrab] = kDeep;
    c[ImGuiCol_Button] = rgb(92, 16, 26, 0.92f);
    c[ImGuiCol_ButtonHovered] = rgb(142, 24, 38, 0.95f);
    c[ImGuiCol_ButtonActive] = rgb(196, 36, 52, 1.f);
    c[ImGuiCol_CheckMark] = rgb(255, 84, 98);
    c[ImGuiCol_SliderGrab] = rgb(200, 40, 55);
    c[ImGuiCol_SliderGrabActive] = rgb(255, 84, 98);
    c[ImGuiCol_TextDisabled] = rgb(110, 84, 86);

    ImPlotStyle &ps = ImPlot::GetStyle();
    ps.PlotBorderSize = 1.f;
    ps.LineWeight = 1.5f;
    ps.MarkerSize = 3.f;
    ps.PlotPadding = ImVec2(10.f, 10.f);
    ImVec4 *pc = ps.Colors;
    pc[ImPlotCol_FrameBg] = rgb(0, 0, 0, 0.f);
    pc[ImPlotCol_PlotBg] = rgb(10, 4, 6, 0.68f);
    pc[ImPlotCol_PlotBorder] = rgb(130, 24, 34, 0.9f);
    pc[ImPlotCol_LegendBg] = rgb(20, 7, 10, 0.85f);
    pc[ImPlotCol_LegendBorder] = rgb(110, 20, 30, 0.9f);
    pc[ImPlotCol_LegendText] = kText;
    pc[ImPlotCol_TitleText] = rgb(255, 120, 126);
    pc[ImPlotCol_InlayText] = kText;
    pc[ImPlotCol_AxisText] = rgb(200, 160, 158);
    pc[ImPlotCol_AxisGrid] = rgb(150, 40, 50, 0.20f);
    pc[ImPlotCol_AxisTick] = rgb(150, 40, 50, 0.45f);
}

// шрифт с нормальным размером, если в системе нашёлся TTF
void load_font() {
    const char *candidates[] = {"/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", "/usr/share/fonts/truetype/ubuntu/Ubuntu-R.ttf",
                                "/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf"};
    for (const char *path : candidates) {
        FILE *f = std::fopen(path, "rb");
        if (!f) continue;
        std::fclose(f);
        if (ImFont *font = ImGui::GetIO().Fonts->AddFontFromFileTTF(path, 17.f)) {
            ImGui::GetIO().FontDefault = font;
            return;
        }
    }
}

// ---------- фон ----------

class Backdrop {
public:
    void draw(ImDrawList *dl, ImVec2 size, double t) {
        // градиент: тёмно-красный сверху, почти чёрный снизу
        const ImU32 top = IM_COL32(36, 8, 13, 255), bottom = IM_COL32(8, 3, 5, 255);
        dl->AddRectFilledMultiColor(ImVec2(0, 0), size, top, top, bottom, bottom);

        if (size.x != w_ || size.y != h_) {
            art_ = build_tribal_art(size.x, size.y);
            w_ = size.x;
            h_ = size.y;
        }
        // лёгкое "дыхание" яркости
        const float pulse = 0.92f + 0.08f * static_cast<float>(std::sin(t * 0.9));
        std::vector<ImVec2> edge;
        // заливка без сглаживания краёв, иначе между соседними четырёхугольниками видны швы
        const ImDrawListFlags saved_flags = dl->Flags;
        dl->Flags &= ~ImDrawListFlags_AntiAliasedFill;
        for (const Blade &b : art_) {
            const ImU32 fill = pack(rgb(150, 15, 25, 0.34f * b.shade * pulse));
            for (size_t i = 0; i + 1 < b.left.size(); ++i)
                dl->AddQuadFilled(ImVec2(b.left[i].x, b.left[i].y), ImVec2(b.left[i + 1].x, b.left[i + 1].y),
                                  ImVec2(b.right[i + 1].x, b.right[i + 1].y), ImVec2(b.right[i].x, b.right[i].y), fill);
        }
        dl->Flags = saved_flags;

        // контуры лезвий
        for (const Blade &b : art_) {
            const ImU32 line = pack(rgb(230, 36, 52, 0.65f * b.shade * pulse));
            for (const std::vector<Pt> *side : {&b.left, &b.right}) {
                edge.clear();
                for (const Pt &p : *side) edge.emplace_back(p.x, p.y);
                dl->AddPolyline(edge.data(), static_cast<int>(edge.size()), line, ImDrawFlags_None, 1.f);
            }
        }
    }

private:
    std::vector<Blade> art_;
    float w_ = 0.f, h_ = 0.f;
};

// ---------- элементы интерфейса ----------

ImVec2 text_size(float size, const char *s) { return ImGui::GetFont()->CalcTextSizeA(size, FLT_MAX, 0.f, s); }

void header(const std::string &subtitle, bool live, double t) {
    ImDrawList *dl = ImGui::GetWindowDrawList();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float width = ImGui::GetContentRegionAvail().x;

    // заголовок и подзаголовок
    ImFont *font = ImGui::GetFont();
    dl->AddText(font, 32.f, ImVec2(p.x, p.y), pack(kCrimson), "PLUTO SDR");
    const float w1 = text_size(32.f, "PLUTO SDR ").x;
    dl->AddText(font, 32.f, ImVec2(p.x + w1, p.y), pack(kMuted), "// RX MONITOR");
    dl->AddText(font, 15.f, ImVec2(p.x + 2.f, p.y + 38.f), pack(kMuted), subtitle.c_str());

    // индикатор LIVE
    const char *label = live ? "LIVE" : "WAITING";
    const ImVec2 ts = text_size(18.f, label);
    const float bw = ts.x + 52.f, bh = 34.f;
    const ImVec2 b0(p.x + width - bw, p.y + 4.f), b1(b0.x + bw, b0.y + bh);
    dl->AddRectFilled(b0, b1, pack(rgb(30, 8, 12, 0.85f)), 17.f);
    dl->AddRect(b0, b1, pack(live ? kCrimson : kDeep), 17.f, 0, 1.5f);
    const float pulse = live ? 0.55f + 0.45f * static_cast<float>(std::sin(t * 5.0)) : 0.35f;
    dl->AddCircleFilled(ImVec2(b0.x + 20.f, b0.y + bh * 0.5f), 6.f, pack(rgb(255, 60, 72, pulse)));
    dl->AddText(font, 18.f, ImVec2(b0.x + 36.f, b0.y + (bh - ts.y) * 0.5f), pack(kText), label);

    ImGui::Dummy(ImVec2(width, 62.f));
}

struct Card {
    const char *label;
    std::string value;
    ImVec4 color;
};

void stat_cards(const Card *cards, int n) {
    ImDrawList *dl = ImGui::GetWindowDrawList();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float gap = 12.f, height = 64.f;
    const float width = (ImGui::GetContentRegionAvail().x - gap * (n - 1)) / n;
    ImFont *font = ImGui::GetFont();
    for (int i = 0; i < n; ++i) {
        const ImVec2 a(p.x + i * (width + gap), p.y), b(a.x + width, a.y + height);
        dl->AddRectFilled(a, b, pack(kPanel), 8.f);
        dl->AddRect(a, b, pack(rgb(120, 22, 32, 0.85f)), 8.f, 0, 1.f);
        dl->AddRectFilled(ImVec2(a.x, a.y + 10.f), ImVec2(a.x + 3.f, b.y - 10.f), pack(kCrimson));  // акцентная полоска
        dl->AddText(font, 12.f, ImVec2(a.x + 14.f, a.y + 8.f), pack(kMuted), cards[i].label);
        dl->AddText(font, 26.f, ImVec2(a.x + 14.f, a.y + 26.f), pack(cards[i].color), cards[i].value.c_str());
    }
    ImGui::Dummy(ImVec2(ImGui::GetContentRegionAvail().x, height));
}

void message_strip(const std::string &text, bool has_frames) {
    ImDrawList *dl = ImGui::GetWindowDrawList();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float width = ImGui::GetContentRegionAvail().x, height = 38.f;
    const ImVec2 b(p.x + width, p.y + height);
    dl->AddRectFilled(p, b, pack(rgb(18, 6, 9, 0.85f)), 8.f);
    dl->AddRect(p, b, pack(rgb(120, 22, 32, 0.85f)), 8.f, 0, 1.f);
    ImFont *font = ImGui::GetFont();
    dl->AddText(font, 12.f, ImVec2(p.x + 14.f, p.y + 12.f), pack(kMuted), "LAST MESSAGE");
    const std::string shown = has_frames ? "\"" + text + "\"" : "waiting for frames...";
    dl->AddText(font, 20.f, ImVec2(p.x + 140.f, p.y + 8.f), pack(has_frames ? kText : kMuted), shown.c_str());
    ImGui::Dummy(ImVec2(width, height));
}

void line_pair(const char *name_i, const char *name_q, const std::vector<float> &i, const std::vector<float> &q) {
    if (i.empty()) return;
    ImPlot::SetNextLineStyle(kLineI, 1.0f);
    ImPlot::PlotLine(name_i, i.data(), static_cast<int>(i.size()));
    ImPlot::SetNextLineStyle(ImVec4(kLineQ.x, kLineQ.y, kLineQ.z, 0.85f), 1.0f);
    ImPlot::PlotLine(name_q, q.data(), static_cast<int>(q.size()));
}

// ---------- режимы правых графиков ----------

enum class View { Symbols, Trajectory, Offset, LiveXY };

struct UiState {
    View view = View::Symbols;
    bool lines = false;     // соединять точки линиями
    bool spin = false;      // вращать картинку
    bool autoanim = false;  // автоматически двигать сдвиг и фазу
    int offset = 0;         // сдвиг точки выборки внутри символа, 0..9
};

struct Cloud {
    std::vector<float> x, y;
    void add(std::complex<float> v) {
        x.push_back(v.real());
        y.push_back(v.imag());
    }
    void rotate(float ang) {
        const float c = std::cos(ang), s = std::sin(ang);
        for (size_t k = 0; k < x.size(); ++k) {
            const float nx = x[k] * c - y[k] * s, ny = x[k] * s + y[k] * c;
            x[k] = nx;
            y[k] = ny;
        }
    }
};

// два облака точек для правой колонки и их подписи
struct Views {
    Cloud top, bottom;
    std::string title_top, title_bottom;
    float marker = 3.5f;
};

// значения фильтра в точках сдвига offset от конца каждого символа последнего кадра
Cloud offset_cloud(const LiveSnapshot &s, int offset, bool corrected) {
    Cloud c;
    if (s.sym_first < 0) return c;
    const int n = static_cast<int>(s.frame_i.size());
    for (int m = PREAMBLE_SYMBOLS; m < s.sym_count; ++m) {
        const int idx = s.sym_first + m * SYM_LEN + offset;
        if (idx >= n) break;
        std::complex<float> v(s.frame_i[idx], s.frame_q[idx]);
        v /= s.amp;
        if (corrected) v *= std::polar(1.f, -(s.theta * static_cast<float>(m) + s.phase));
        c.add(v);
    }
    return c;
}

// последние n отсчётов окна: сырые или после фильтра, в единицах амплитуды кадра
Cloud live_cloud(const LiveSnapshot &s, size_t n, bool filtered) {
    Cloud c;
    const size_t total = s.win_i.size();
    if (total == 0) return c;
    std::vector<cf> x(total);
    for (size_t k = 0; k < total; ++k) x[k] = cf(s.win_i[k], s.win_q[k]);
    if (filtered) x = boxcar_mf(x);
    for (size_t k = total > n ? total - n : 0; k < total; ++k) c.add(x[k] / s.amp);
    return c;
}

// последний кадр после фильтра с компенсацией частоты и фазы (непрерывный путь)
Cloud frame_path(const LiveSnapshot &s) {
    Cloud c;
    if (s.sym_first < 0) return c;
    for (size_t k = 0; k < s.frame_i.size(); ++k) {
        const float sym = (static_cast<float>(k) - static_cast<float>(s.sym_first)) / SYM_LEN;
        std::complex<float> v(s.frame_i[k], s.frame_q[k]);
        c.add(v / s.amp * std::polar(1.f, -(s.theta * sym + s.phase)));
    }
    return c;
}

Views build_views(const LiveSnapshot &s, const UiState &ui, double t) {
    Views v;
    const int offset = ui.autoanim ? static_cast<int>(t * 3.0) % SYM_LEN : ui.offset;
    switch (ui.view) {
        case View::Symbols:
            v.top.x = s.raw_x;
            v.top.y = s.raw_y;
            v.bottom.x = s.cor_x;
            v.bottom.y = s.cor_y;
            v.title_top = "CONSTELLATION  |  raw###c1";
            v.title_bottom = "CONSTELLATION  |  corrected###c2";
            break;
        case View::Trajectory:
            v.top = live_cloud(s, 4096, true);
            v.bottom = frame_path(s);
            v.title_top = "TRAJECTORY  |  live, after filter###c1";
            v.title_bottom = "TRAJECTORY  |  last frame, corrected###c2";
            v.marker = 1.8f;
            break;
        case View::Offset:
            v.top = offset_cloud(s, offset, false);
            v.bottom = offset_cloud(s, offset, true);
            v.title_top = "OFFSET " + std::to_string(offset) + "  |  raw###c1";
            v.title_bottom = "OFFSET " + std::to_string(offset) + "  |  corrected###c2";
            break;
        case View::LiveXY:
            v.top = live_cloud(s, 2048, false);
            v.bottom = live_cloud(s, 2048, true);
            v.title_top = "LIVE XY  |  antenna I/Q###c1";
            v.title_bottom = "LIVE XY  |  after matched filter###c2";
            v.marker = 2.2f;
            break;
    }
    // вращение и автоматическое покачивание фазы
    float ang = ui.spin ? static_cast<float>(t) * 1.1f : 0.f;
    if (ui.autoanim) ang += 0.35f * static_cast<float>(std::sin(t * 0.7));
    if (ang != 0.f) {
        v.top.rotate(ang);
        v.bottom.rotate(ang);
    }
    return v;
}

// график точек: по Y всегда +-1.9, по X растягиваем под пропорции ячейки (берём с прошлого кадра)
void cloud_plot(const std::string &title, const Cloud &c, const ImVec4 &color, bool lines, float marker, float &ratio) {
    if (!ImPlot::BeginPlot(title.c_str())) return;
    ImPlot::SetupAxes("I", "Q", ImPlotAxisFlags_Lock, ImPlotAxisFlags_Lock);
    const double half = 1.9;
    ImPlot::SetupAxisLimits(ImAxis_X1, -half * ratio, half * ratio, ImPlotCond_Always);
    ImPlot::SetupAxisLimits(ImAxis_Y1, -half, half, ImPlotCond_Always);
    if (!c.x.empty()) {
        const int n = static_cast<int>(c.x.size());
        if (lines) {
            ImPlot::SetNextLineStyle(ImVec4(color.x, color.y, color.z, 0.55f), 1.0f);
            ImPlot::PlotLine("path##l", c.x.data(), c.y.data(), n);
        }
        ImPlot::SetNextMarkerStyle(ImPlotMarker_Circle, marker, color, IMPLOT_AUTO, color);
        ImPlot::PlotScatter("points", c.x.data(), c.y.data(), n);
    }
    const ImVec2 sz = ImPlot::GetPlotSize();
    if (sz.y > 1.f) ratio = sz.x / sz.y;
    ImPlot::EndPlot();
}

// сетка 2x2: слева широкие временные графики, справа облака точек выбранного режима
void plots(const LiveSnapshot &s, const Views &v, const UiState &ui) {
    static float col_ratios[2] = {1.7f, 1.f};
    static float ratio_top = 1.6f, ratio_bottom = 1.6f;
    if (!ImPlot::BeginSubplots("##grid", 2, 2, ImVec2(-1, -1), ImPlotSubplotFlags_NoResize, nullptr, col_ratios)) return;

    if (ImPlot::BeginPlot("RX STREAM  |  last 8192 samples")) {
        ImPlot::SetupAxes("sample", "amplitude", ImPlotAxisFlags_AutoFit, ImPlotAxisFlags_AutoFit);
        line_pair("I", "Q", s.win_i, s.win_q);
        ImPlot::EndPlot();
    }
    cloud_plot(v.title_top, v.top, kDotRaw, ui.lines, v.marker, ratio_top);
    if (ImPlot::BeginPlot("LAST FRAME  |  after matched filter")) {
        ImPlot::SetupAxes("sample", "amplitude", ImPlotAxisFlags_AutoFit, ImPlotAxisFlags_AutoFit);
        line_pair("I", "Q", s.frame_i, s.frame_q);
        ImPlot::EndPlot();
    }
    cloud_plot(v.title_bottom, v.bottom, kDotFix, ui.lines, v.marker, ratio_bottom);
    ImPlot::EndSubplots();
}

// ---------- панель управления ----------

// кнопка режима: активная подсвечена
bool mode_button(const char *label, bool active) {
    if (active) {
        ImGui::PushStyleColor(ImGuiCol_Button, rgb(190, 34, 50));
        ImGui::PushStyleColor(ImGuiCol_Text, rgb(255, 255, 255));
    }
    const bool pressed = ImGui::Button(label, ImVec2(104.f, 28.f));
    if (active) ImGui::PopStyleColor(2);
    return pressed;
}

void control_panel(UiState &ui) {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, rgb(18, 6, 9, 0.85f));
    ImGui::BeginChild("##controls", ImVec2(0.f, 50.f), ImGuiChildFlags_Borders, ImGuiWindowFlags_NoScrollbar);
    ImGui::SetCursorPosY(10.f);

    struct Entry {
        const char *label;
        View view;
    };
    const Entry entries[] = {{"Symbols", View::Symbols}, {"Trajectory", View::Trajectory}, {"Offset", View::Offset}, {"Live XY", View::LiveXY}};
    for (const Entry &e : entries) {
        if (mode_button(e.label, ui.view == e.view)) {
            ui.view = e.view;
            if (e.view == View::Trajectory) ui.lines = true;  // путь сигнала показываем линией
        }
        ImGui::SameLine();
    }

    ImGui::SameLine(0.f, 18.f);
    ImGui::Checkbox("Lines", &ui.lines);
    ImGui::SameLine();
    ImGui::Checkbox("Spin", &ui.spin);
    ImGui::SameLine();
    ImGui::Checkbox("Auto", &ui.autoanim);

    // сдвиг доступен только в режиме Offset и вручную
    ImGui::SameLine(0.f, 18.f);
    ImGui::BeginDisabled(ui.view != View::Offset || ui.autoanim);
    ImGui::SetNextItemWidth(170.f);
    ImGui::SliderInt("Offset", &ui.offset, 0, SYM_LEN - 1);
    ImGui::EndDisabled();

    ImGui::SameLine(0.f, 18.f);
    if (ImGui::Button("Reset", ImVec2(80.f, 28.f))) ui = UiState();

    ImGui::EndChild();
    ImGui::PopStyleColor();
}

std::string fmt(const char *pattern, double v) {
    char buf[48];
    std::snprintf(buf, sizeof buf, pattern, v);
    return buf;
}

// ---------- окно SDL + OpenGL + ImGui (освобождает всё в деструкторе) ----------

class Window {
public:
    bool open() {
        if (SDL_Init(SDL_INIT_VIDEO) != 0) {
            std::printf("SDL_Init: %s\n", SDL_GetError());
            return false;
        }
        sdl_ = true;
        win_ = SDL_CreateWindow("PLUTO SDR // RX MONITOR", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1360, 860,
                                SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
        if (!win_) {
            std::printf("SDL_CreateWindow: %s\n", SDL_GetError());
            return false;
        }
        gl_ = SDL_GL_CreateContext(win_);
        if (!gl_) {
            std::printf("SDL_GL_CreateContext: %s\n", SDL_GetError());
            return false;
        }
        SDL_GL_MakeCurrent(win_, gl_);
        SDL_GL_SetSwapInterval(1);

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImPlot::CreateContext();
        load_font();
        apply_theme();
        ImGui_ImplSDL2_InitForOpenGL(win_, gl_);
        ImGui_ImplOpenGL3_Init("#version 330");
        ui_ = true;
        return true;
    }

    // false, когда окно закрыли
    bool pump_events() {
        bool alive = true;
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            ImGui_ImplSDL2_ProcessEvent(&ev);
            if (ev.type == SDL_QUIT) alive = false;
            if (ev.type == SDL_WINDOWEVENT && ev.window.event == SDL_WINDOWEVENT_CLOSE) alive = false;
            if (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_ESCAPE) alive = false;
        }
        return alive;
    }

    void begin_frame() {
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();
    }

    void end_frame() {
        ImGui::Render();
        const ImGuiIO &io = ImGui::GetIO();
        glViewport(0, 0, static_cast<int>(io.DisplaySize.x * io.DisplayFramebufferScale.x),
                   static_cast<int>(io.DisplaySize.y * io.DisplayFramebufferScale.y));
        glClearColor(0.043f, 0.020f, 0.027f, 1.f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        SDL_GL_SwapWindow(win_);
    }

    ~Window() {
        if (ui_) {
            ImGui_ImplOpenGL3_Shutdown();
            ImGui_ImplSDL2_Shutdown();
            ImPlot::DestroyContext();
            ImGui::DestroyContext();
        }
        if (gl_) SDL_GL_DeleteContext(gl_);
        if (win_) SDL_DestroyWindow(win_);
        if (sdl_) SDL_Quit();
    }

private:
    SDL_Window *win_ = nullptr;
    SDL_GLContext gl_ = nullptr;
    bool sdl_ = false, ui_ = false;
};

}  // namespace

int run_gui(LiveData &live, const std::string &subtitle) {
    Window window;
    if (!window.open()) return 1;

    Backdrop backdrop;
    UiState ui;
    while (!g_stop && window.pump_events()) {
        const LiveSnapshot s = live.snapshot();
        const double t = ImGui::GetTime();

        window.begin_frame();
        const ImVec2 size = ImGui::GetIO().DisplaySize;
        backdrop.draw(ImGui::GetBackgroundDrawList(), size, t);

        // один прозрачный корневой слой на весь экран
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(size);
        ImGui::Begin("##root", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                         ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoBringToFrontOnFocus);

        header(subtitle, s.buffers > 0, t);

        const Card cards[] = {
            {"BUFFERS", std::to_string(s.buffers), kText},
            {"FRAMES OK", std::to_string(s.frames_ok), kCrimson},
            {"CRC ERRORS", std::to_string(s.frames_bad), s.frames_bad ? kWarn : kText},
            {"CFO, Hz", fmt("%+.1f", s.cfo_hz), kText},
            {"SNR, dB", fmt("%.1f", s.snr_db), kText},
            {"AMPLITUDE", fmt("%.1f", s.amplitude), kText},
        };
        stat_cards(cards, 6);
        message_strip(s.last_text, s.frames_ok > 0);
        control_panel(ui);
        plots(s, build_views(s, ui, t), ui);

        ImGui::End();
        window.end_frame();
    }
    return 0;
}

}  // namespace sdr
