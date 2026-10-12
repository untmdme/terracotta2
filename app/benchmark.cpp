// Terracotta2 benchmark: opens one 1600x900 window, mutates the UI and calls
// Window::renderFrame() directly, then prints a results table and a "Bottlenecks" summary.
//
//   Benchmark                run everything, print tables, exit
//   Benchmark --interactive  keep the last scene open (run with TC2_SHOW_DAMAGE=1 to see damage rects)
//   Benchmark --scale=0.1    scale row/paragraph counts (quick smoke test)
//
// What the numbers mean: cpuMs/layoutMs/paintMs/submitMs come from FrameStats. submitMs includes
// present / vsync waits (Fifo), so it is a floor set by the display, not pure CPU work. GPU
// execution time is NOT measured. "mut" is wall time spent in the mutation itself (setScrollY,
// Label::text, tree walks, ...) outside renderFrame().
#include <terracotta2/Core/Application.hpp>
#include <terracotta2/Core/Window.hpp>
#include <terracotta2/Theme/Theme.hpp>
#include <terracotta2/UI/Container.hpp>
#include <terracotta2/UI/Controls.hpp>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <psapi.h>
#else
#include <mach/mach.h>
#endif

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <functional>
#include <random>
#include <string>
#include <thread>
#include <vector>

using namespace Terracotta2;
using Clock = std::chrono::steady_clock;

namespace {

// ---------------------------------------------------------------- measurement helpers

double msSince(Clock::time_point t) { return std::chrono::duration<double, std::milli>(Clock::now() - t).count(); }

double residentMB() {
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS pmc;
    if (!GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) return 0;
    return pmc.WorkingSetSize / (1024.0 * 1024.0);
#else
    mach_task_basic_info info;
    mach_msg_type_number_t n = MACH_TASK_BASIC_INFO_COUNT;
    if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO, (task_info_t)&info, &n) != KERN_SUCCESS) return 0;
    return info.resident_size / (1024.0 * 1024.0);
#endif
}

struct Series {
    std::vector<double> v;
    void add(double x) { v.push_back(x); }
    double at(double q) const { // nearest-rank quantile
        if (v.empty()) return 0;
        auto s = v;
        std::sort(s.begin(), s.end());
        return s[std::min(s.size() - 1, (size_t)std::max(0.0, std::ceil(q * s.size()) - 1))];
    }
    double med() const { return at(0.5); }
    double p95() const { return at(0.95); }
    double mean() const {
        double t = 0;
        for (double x : v) t += x;
        return v.empty() ? 0 : t / v.size();
    }
};

struct Setup { // cost of building a scene and rendering its first frame
    double buildMs = -1, firstMs = 0, firstCpu = 0, firstLayout = 0, firstPaint = 0, memMB = 0;
};

struct Result {
    std::string id, title;
    int frames = 0, layoutFrames = 0;
    Series cpu, layout, paint, submit, mut;
    double widgets = 0, dmgPct = 0;
    Setup setup;
    double memEnd = 0, memCleared = -1;
};

bool g_idlePass = false;
double g_idleUsPerCall = 0, g_warmupMs = 0;
double g_scale = 1.0;
int scaled(int n) { return std::max(100, (int)(n * g_scale)); }

// ---------------------------------------------------------------- scene helpers

constexpr SkColor kMuted = SkColorSetRGB(140, 140, 150);
constexpr SkColor kPalette[] = {
    SkColorSetRGB(214, 90, 70),  SkColorSetRGB(230, 160, 60), SkColorSetRGB(110, 180, 90),
    SkColorSetRGB(60, 160, 190), SkColorSetRGB(100, 120, 210), SkColorSetRGB(170, 100, 200),
};
const char* const kLorem =
    "Lorem ipsum dolor sit amet, consectetur adipiscing elit, sed do eiusmod tempor incididunt ut labore et dolore "
    "magna aliqua. Ut enim ad minim veniam, quis nostrud exercitation ullamco laboris nisi ut aliquip ex ea commodo "
    "consequat. Duis aute irure dolor in reprehenderit in voluptate velit esse cillum dolore eu fugiat nulla pariatur. "
    "Excepteur sint occaecat cupidatat non proident, sunt in culpa qui officia deserunt mollit anim id est laborum. ";

SkColor lerp(SkColor a, SkColor b, float t) {
    auto ch = [&](int x, int y) { return (int)(x + (y - x) * t); };
    return SkColorSetRGB(ch(SkColorGetR(a), SkColorGetR(b)), ch(SkColorGetG(a), SkColorGetG(b)), ch(SkColorGetB(a), SkColorGetB(b)));
}

std::string rowText(int i, bool wrap) {
    std::string s = "Item " + std::to_string(i) + " - status report for entity " + std::to_string(i * 7919 % 10007);
    if (wrap) s += ". " + std::string(kLorem).substr(0, 240); // long enough to wrap at ~1200 px
    return s;
}

// index label | text label (grows, wraps if long) | status chip
struct RowRefs { Label* idx; Label* txt; Panel* chip; };
RowRefs makeRow(Widget& parent, int i, bool wrap) {
    auto& box = parent.add<HBox>().padding(YGEdgeHorizontal, 12).padding(YGEdgeVertical, 4).gap(10).alignItems(YGAlignCenter);
    RowRefs r;
    r.idx = &box.add<Label>(std::to_string(i), 12.0f).width(60).color(kMuted);
    r.txt = &box.add<Label>(rowText(i, wrap)).grow(1).shrink(1).basis(0);
    r.chip = &box.add<Panel>(kPalette[i % 6]).width(36).height(12);
    return r;
}

struct ListScene { // sidebar | scrolling list of N rows, all Yoga
    Panel* sidebar = nullptr;
    ScrollView* scroll = nullptr;
    std::vector<RowRefs> rows;
};
ListScene buildList(Widget& root, int n) {
    ListScene s;
    s.rows.reserve(n);
    auto& split = root.add<HBox>().grow(1).basis(0);
    s.sidebar = &split.add<Panel>(SkColorSetRGB(40, 40, 48)).width(200);
    s.scroll = &split.add<ScrollView>();
    for (int i = 0; i < n; ++i) s.rows.push_back(makeRow(*s.scroll, i, i % 4 == 0));
    return s;
}

// Manual-layout virtualized list: `rows` logical rows, a fixed pool of recycled row widgets.
class VirtualList : public Widget {
public:
    static constexpr float kRowH = 24.0f;
    static constexpr int kPool = 40; // 900/24 = 37.5 visible + margin

    explicit VirtualList(int rows) : m_rows(rows) {
        manualLayout(); // before adding children
        grow(1).basis(0);
        for (int k = 0; k < kPool; ++k) m_slots.push_back({makeRow(*this, -1, false), -1});
        setScrollY(0);
    }
    float scrollY() const { return m_scroll; }
    void setScrollY(float y) {
        m_scroll = std::clamp(y, 0.0f, std::max(0.0f, m_rows * kRowH - m_bounds.height()));
        const int first = (int)(m_scroll / kRowH);
        for (int k = 0; k < kPool; ++k) { // recycle: rebind slot k to row first+k
            Slot& s = m_slots[k];
            const int r = first + k;
            if (s.row == r || r >= m_rows) continue;
            s.row = r;
            s.refs.idx->text(std::to_string(r));
            s.refs.txt->text(rowText(r, false));
            s.refs.chip->background(kPalette[r % 6]);
        }
        place();
    }
    void onLayout() override { place(); } // resize: re-fit widths

private:
    struct Slot { RowRefs refs; int row; };
    void place() {
        for (int k = 0; k < kPool; ++k) {
            Slot& s = m_slots[k];
            Widget* box = s.refs.idx->parent();
            const bool live = s.row >= 0 && s.row < m_rows;
            box->visible(live);
            if (live) box->setFrame(SkRect::MakeXYWH(0, s.row * kRowH - m_scroll, m_bounds.width(), kRowH));
        }
    }
    int m_rows;
    float m_scroll = 0;
    std::vector<Slot> m_slots;
};

// ---------------------------------------------------------------- the harness

struct Bench {
    Application app;
    Window* win = nullptr;
    Widget* root = nullptr;
    double winArea = 1;
    std::vector<Result> results;

    Bench() {
        win = app.createWindow({.title = "Terracotta2 Benchmark", .width = 1600, .height = 900});
        root = win->getRootWidget();
        winArea = win->size().width() * win->size().height();
        // Wait until the window can present, then start from an empty root.
        // Warm-up scene: first text draw / first frame pays shader + glyph-atlas setup, which
        // would otherwise pollute scenario 1's "first frame".
        auto t0 = Clock::now();
        for (int w : {400, 700}) {
            auto& p = root->add<Panel>(SkColorSetRGB(60, 60, 70));
            for (int i = 0; i < 8; ++i) p.add<Label>("Warm-up text 0123456789 " + std::to_string(i), 10.0f + 2 * i, w);
            p.add<Panel>(kPalette[0]).width(30).height(12);
        }
        for (int i = 0; i < 400; ++i) {
            app.pumpOnce(false);
            if (win->renderFrame() || win->lastFrameStats().frameIndex > 0) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        warmupMs = msSince(t0);
        clear(false);
    }
    double warmupMs = 0;

    void drain(int max = 8) { for (int i = 0; i < max && win->needsFrame(); ++i) win->renderFrame(); }

    void clear(bool record = true) {
        root->clearChildren();
        drain();
        if (record && !results.empty()) {
            results.back().memCleared = residentMB();
            std::printf("  [rss after clear: %.0f MB]\n", results.back().memCleared);
        }
    }

    // Build a scene, then render until its first frame is presented.
    Setup build(const std::function<void()>& fn) {
        Setup s;
        auto t0 = Clock::now();
        fn();
        s.buildMs = msSince(t0);
        t0 = Clock::now();
        for (int i = 0; i < 50 && !win->renderFrame(); ++i) {}
        const FrameStats& f = win->lastFrameStats();
        s.firstMs = msSince(t0);
        s.firstCpu = f.cpuMs; s.firstLayout = f.layoutMs; s.firstPaint = f.paintMs;
        drain();
        s.memMB = residentMB();
        return s;
    }

    // Run `steps` iterations of: mutate(i) -> one renderFrame -> record. Optionally stop early once idle.
    Result& run(std::string id, std::string title, int steps, const std::function<void(int)>& mutate,
                Setup setup = {}, bool untilIdle = false) {
        std::fprintf(stderr, "running %s %s ...\n", id.c_str(), title.c_str());
        Result r;
        r.id = std::move(id); r.title = std::move(title); r.setup = setup;
        if (r.setup.memMB == 0) r.setup.memMB = residentMB();
        double dmg = 0, widgets = 0;
        for (int i = 0; i < steps; ++i) {
            if (untilIdle && i > 0 && !win->needsFrame()) break;
            auto t0 = Clock::now();
            mutate(i);
            r.mut.add(msSince(t0));
            if (win->renderFrame()) record(r, win->lastFrameStats(), dmg, widgets);
            if (!untilIdle) drain(); // animations must not be advanced by unrecorded frames
            if (i % 16 == 15 && !win->needsFrame()) app.pumpOnce(false); // keep the OS happy
        }
        if (r.frames) { r.widgets = widgets / r.frames; r.dmgPct = dmg / r.frames / winArea * 100.0; }
        r.memEnd = residentMB();
        results.push_back(std::move(r));
        return results.back();
    }

    // A scenario with no steps: the first frame is the measurement.
    Result& firstFrameOnly(std::string id, std::string title, Setup s) {
        Result r;
        r.id = std::move(id); r.title = std::move(title); r.setup = s;
        r.frames = 1; r.layoutFrames = 1;
        r.cpu.add(s.firstCpu); r.layout.add(s.firstLayout); r.paint.add(s.firstPaint);
        r.submit.add(std::max(0.0, s.firstCpu - s.firstLayout - s.firstPaint)); // incl. vsync wait
        r.mut.add(0);
        r.memEnd = residentMB();
        results.push_back(std::move(r));
        return results.back();
    }

private:
    void record(Result& r, const FrameStats& f, double& dmg, double& widgets) {
        ++r.frames;
        r.layoutFrames += f.layoutRan;
        r.cpu.add(f.cpuMs); r.layout.add(f.layoutMs); r.paint.add(f.paintMs); r.submit.add(f.submitMs);
        widgets += f.widgetsPainted;
        dmg += f.damageArea;
    }
};

// ---------------------------------------------------------------- reporting

std::string pair(const Series& s) {
    char b[48];
    std::snprintf(b, sizeof b, "%6.2f /%7.2f", s.med(), s.p95());
    return b;
}

void printTables(const std::vector<Result>& rs) {
    std::printf("\nWarm-up (GPU pipelines, glyph atlas, empty window) took %.0f ms before scenario 1.\n", g_warmupMs);
    std::printf("\n=== Frame cost per scenario (ms, median / p95) ===\n");
    std::printf("%-34s %6s %-15s %-15s %-15s %-15s %-15s %8s %7s %6s\n", "scenario", "frames", "cpu", "layout", "paint",
                "submit*", "mut", "widgets", "dmg%", "layout");
    for (const auto& r : rs) {
        std::string name = r.id + " " + r.title;
        std::printf("%-34s %6d %-15s %-15s %-15s %-15s %-15s %8.0f %7.2f %3d/%-3d\n", name.c_str(), r.frames,
                    pair(r.cpu).c_str(), pair(r.layout).c_str(), pair(r.paint).c_str(), pair(r.submit).c_str(),
                    pair(r.mut).c_str(), r.widgets, r.dmgPct, r.layoutFrames, r.frames);
    }
    std::printf("* submitMs includes present / vsync waits (Fifo); GPU execution time is not measured.\n");
    std::printf("  mut = wall time of the mutation itself (outside renderFrame). 'layout' col = frames that ran Yoga.\n");

    std::printf("\n=== Setup and memory ===\n");
    std::printf("%-34s %10s %10s %9s %9s %9s %10s %10s\n", "scenario", "build ms", "1st frame", "1st cpu", "1st layout",
                "1st paint", "rss setup", "rss cleared");
    for (const auto& r : rs) {
        std::string name = r.id + " " + r.title;
        char build[16] = "(shared)";
        if (r.setup.buildMs >= 0) std::snprintf(build, sizeof build, "%.1f", r.setup.buildMs);
        char cleared[16] = "-";
        if (r.memCleared >= 0) std::snprintf(cleared, sizeof cleared, "%.0f MB", r.memCleared);
        if (r.setup.buildMs >= 0)
            std::printf("%-34s %10s %10.1f %9.1f %9.1f %9.1f %7.0f MB %10s\n", name.c_str(), build, r.setup.firstMs,
                        r.setup.firstCpu, r.setup.firstLayout, r.setup.firstPaint, r.setup.memMB, cleared);
        else
            std::printf("%-34s %10s %10s %9s %9s %9s %7.0f MB %10s\n", name.c_str(), build, "-", "-", "-", "-",
                        r.setup.memMB, cleared);
    }
}

const Result* find(const std::vector<Result>& rs, const char* id) {
    for (const auto& r : rs) if (r.id == id) return &r;
    return nullptr;
}

// Display frame period estimate: median cpu time of the cheap, many-frame scenarios (they are pure
// present-wait paced). ~8.3 ms on a 120 Hz ProMotion panel, ~16.6 ms at 60 Hz.
double framePeriodMs(const std::vector<Result>& rs) {
    Series s;
    for (const auto& r : rs)
        if (r.frames >= 30 && r.layout.med() + r.paint.med() + r.mut.med() < 1.5) s.add(r.cpu.med());
    return s.v.empty() ? 16.6 : s.med();
}

void printBottlenecks(const std::vector<Result>& rs) {
    constexpr double kBudget = 16.6;
    const double period = framePeriodMs(rs);
    std::printf("\n=== Bottlenecks ===\n");
    std::printf("Estimated display frame period: %.1f ms (%.0f Hz). Budget checked against 16.6 ms, and against the period if shorter.\n",
                period, 1000.0 / period);
    std::printf("A scenario's submit* is treated as present wait (not work) when it is >= 40%% of that period and frames >= 10;\n"
                "it is shown but excluded from the ranking.\n");
    auto vsyncBound = [&](const Result& r) { return r.frames >= 10 && r.submit.med() >= 0.4 * period; };

    std::printf("Dominant phase per scenario (median layout / paint / submit*, plus mutation time 'mut'):\n");
    for (const auto& r : rs) {
        const double l = r.layout.med(), p = r.paint.med(), s = r.submit.med(), m = r.mut.med();
        const bool vsync = vsyncBound(r);
        const double top = std::max({l, p, m, vsync ? 0.0 : s});
        const char* who = r.frames == 0 ? "-" : top == s && !vsync ? "submit" : top == l ? "layout" : top == p ? "paint" : "mutation";
        const double work = l + p + m; // CPU work excluding present/vsync
        std::printf("  %-34s %-8s (L %.2f  P %.2f  S* %.2f%s  M %.2f)  work(L+P+M) %.2f ms%s\n",
                    (r.id + " " + r.title).c_str(), who, l, p, s, vsync ? " vsync-bound" : "", m, work,
                    work > kBudget ? "  OVER-BUDGET" : work > period ? "  over display period" : "");
    }

    std::printf("\nFrame-budget check (16.6 ms): work = layout+paint+mut (p95 of each summed, conservative); cpu incl. submit/vsync:\n");
    bool any = false;
    for (const auto& r : rs) {
        const double work = r.layout.p95() + r.paint.p95() + r.mut.p95();
        if (work > kBudget || (!vsyncBound(r) && r.cpu.p95() > kBudget)) {
            any = true;
            std::printf("  %-34s work %.2f ms, cpu med %.2f / p95 %.2f ms\n", (r.id + " " + r.title).c_str(), work,
                        r.cpu.med(), r.cpu.p95());
        }
    }
    if (!any) std::printf("  nothing exceeds the budget\n");

    std::printf("\nChecks:\n");
    std::printf("  idle (2): %s, %.3f us per needsFrame()+renderFrame() when idle\n", g_idlePass ? "PASS" : "FAIL",
                g_idleUsPerCall);
    auto ratio = [&](const char* a, const char* b, const char* what) {
        const Result *x = find(rs, a), *y = find(rs, b);
        if (!x || !y) return;
        auto w = [](const Result& r) { return r.layout.med() + r.paint.med() + r.mut.med(); };
        std::printf("  %s: %.2f ms vs %.2f ms work (%.1fx); cpu med %.2f vs %.2f ms (%.1fx)\n", what, w(*x), w(*y),
                    w(*y) > 0 ? w(*x) / w(*y) : 0.0, x->cpu.med(), y->cpu.med(),
                    y->cpu.med() > 0 ? x->cpu.med() / y->cpu.med() : 0.0);
    };
    ratio("4", "7", "scroll Yoga list (4) vs virtual list (7)");
    const Result *a5 = find(rs, "5a"), *b5 = find(rs, "5b");
    if (a5 && b5)
        std::printf("  damage (separate rects): 20 clustered chips = %.1f%% of window / %.0f widgets painted; "
                    "300 scattered chips = %.1f%% / %.0f widgets\n", a5->dmgPct, a5->widgets, b5->dmgPct, b5->widgets);
    if (const Result* t = find(rs, "3"))
        std::printf("  single-label update (3): %.2f%% damage, %.0f widgets painted, layout med %.2f ms (Yoga re-run "
                    "from the dirty path), mut %.3f ms\n", t->dmgPct, t->widgets, t->layout.med(), t->mut.med());
    const Result *a6 = find(rs, "6a"), *b6 = find(rs, "6b");
    if (a6 && b6)
        std::printf("  full relayout (6): width toggle layout med %.2f ms (Yoga caches repeat widths), distinct-width "
                    "sweep %.2f ms\n", a6->layout.med(), b6->layout.med());
    if (const Result* t = find(rs, "8"))
        std::printf("  text reflow (8): layout med %.2f ms, paint med %.2f ms per width change\n", t->layout.med(),
                    t->paint.med());
    if (const Result* t = find(rs, "9"))
        std::printf("  theme repaint (9): paint med %.2f ms, mut (tree walk) med %.2f ms, no layout (%d/%d frames ran Yoga)\n",
                    t->paint.med(), t->mut.med(), t->layoutFrames, t->frames);
    if (const Result* t = find(rs, "1"))
        std::printf("  big list (1): build %.0f ms, first frame %.0f ms (layout %.0f ms), rss %.0f MB\n",
                    t->setup.buildMs, t->setup.firstMs, t->setup.firstLayout, t->setup.memMB);
}

} // namespace

// ---------------------------------------------------------------- scenarios

int main(int argc, char** argv) {
    bool interactive = false;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--interactive")) interactive = true;
        else if (!std::strncmp(argv[i], "--scale=", 8)) g_scale = std::atof(argv[i] + 8);
    }

    Bench b;
    Widget& root = *b.root;
    g_warmupMs = b.warmupMs;
    const int nRows = scaled(10000);

    // ---- 1. big list via Yoga ----
    ListScene list;
    {
        Setup s = b.build([&] { list = buildList(root, nRows); });
        b.firstFrameOnly("1", "Big list (Yoga, " + std::to_string(nRows) + " rows)", s);
    }

    // ---- 2. idle check ----
    {
        b.drain();
        const bool a = !b.win->needsFrame(), c = !b.win->renderFrame();
        b.app.pumpOnce(false);
        const bool d = !b.win->needsFrame();
        g_idlePass = a && c && d;
        auto t0 = Clock::now();
        for (int i = 0; i < 100000; ++i) { b.win->needsFrame(); b.win->renderFrame(); }
        g_idleUsPerCall = msSince(t0) * 1000.0 / 100000;
        std::printf("2 idle check: needsFrame()=false %s, renderFrame()=false %s, after pumpOnce %s => %s\n",
                    a ? "ok" : "BAD", c ? "ok" : "BAD", d ? "ok" : "BAD", g_idlePass ? "PASS" : "FAIL");
        Result r; r.id = "2"; r.title = "Idle"; r.setup.memMB = residentMB(); r.setup.buildMs = -1;
        b.results.push_back(std::move(r));
    }

    // ---- 3. single label update, visible rows (short rows only, so height never changes) ----
    b.run("3", "Single label update x100", 100, [&](int i) {
        int r = 1 + i % 19;
        if (r % 4 == 0) ++r; // rows divisible by 4 are the wrapping ones
        list.rows[r].txt->text("Item " + std::to_string(r) + " - updated #" + std::to_string(i));
    });

    // ---- 4. scroll the Yoga list ----
    b.run("4", "Scroll Yoga list x240", 240, [&](int i) { list.scroll->setScrollY((i + 1) * 13.0f); });
    list.scroll->setScrollY(0);
    b.drain();

    // ---- 9. theme change + full paint-dirty (reuses the list scene; no layout) ----
    {
        std::function<void(Widget&)> dirty = [&](Widget& w) {
            w.markPaintDirty();
            for (auto& c : w.children()) dirty(*c);
        };
        const Theme saved = theme();
        b.run("9", "Theme swap + markPaintDirty x30", 30, [&](int i) {
            theme().globalBackground = (i & 1) ? SkColorSetRGB(235, 235, 240) : SkColorSetRGB(20, 20, 20);
            theme().panelBackground = (i & 1) ? SkColorSetRGB(210, 210, 220) : SkColorSetRGB(35, 35, 35);
            dirty(root);
        });
        theme() = saved;
    }

    // ---- 6. full relayout: sidebar width changes, Yoga re-runs over every row ----
    b.run("6a", "Relayout: sidebar toggle x60", 60, [&](int i) { list.sidebar->width(i & 1 ? 400.0f : 200.0f); });
    b.run("6b", "Relayout: sidebar sweep x60", 60, [&](int i) { list.sidebar->width(200.0f + 3.0f * i); });
    list = {};
    b.clear();

    // ---- 5. animations: grid of chips ----
    {
        std::vector<Panel*> chips;
        Setup s = b.build([&] {
            auto& grid = root.add<HBox>().grow(1).basis(0).wrap(YGWrapWrap).padding(8);
            for (int i = 0; i < 1000; ++i) chips.push_back(&grid.add<Panel>(kPalette[i % 6]).size(30, 30).margin(4));
        });
        auto animateChips = [&](const std::vector<int>& idx) {
            for (int i : idx) {
                Panel* c = chips[i];
                const SkColor from = kPalette[i % 6], to = kPalette[(i + 3) % 6];
                c->animate(1.0f, [c, from, to](float t) { c->background(lerp(from, to, t)); });
            }
        };
        std::vector<int> clustered(20);
        for (int i = 0; i < 20; ++i) clustered[i] = i; // first 20 chips: one strip at the top-left
        std::vector<int> scattered(900); // chips visible in the window (~943 fit)
        for (int i = 0; i < 900; ++i) scattered[i] = i;
        std::shuffle(scattered.begin(), scattered.end(), std::mt19937(1));
        scattered.resize(300);

        b.run("5a", "Animate 20 clustered chips (1 s)", 400, [&](int i) { if (i == 0) animateChips(clustered); }, s, true);
        b.run("5b", "Animate 300 scattered chips (1 s)", 400, [&](int i) { if (i == 0) animateChips(scattered); }, {}, true);
        b.clear();
    }

    // ---- 7. virtualized list, manual layout ----
    {
        const int logical = scaled(100000);
        VirtualList* vl = nullptr;
        Setup s = b.build([&] { vl = &root.add<VirtualList>(logical); });
        b.run("7", "Scroll virtual list x240 (" + std::to_string(logical) + " logical)", 240,
              [&](int i) { vl->setScrollY((i + 1) * 13.0f); }, s);
        b.clear();
    }

    // ---- 8. text-heavy: wrapping paragraphs, then reflow on width change ----
    {
        const int n = scaled(2000);
        VBox* col = nullptr;
        Setup s = b.build([&] {
            auto& sv = root.add<ScrollView>().padding(16);
            col = &sv.add<VBox>().width(900).gap(10);
            const std::string lorem = std::string(kLorem) + kLorem;
            for (int i = 0; i < n; ++i)
                col->add<Label>(lorem.substr((i * 13) % 100, 80 + (i * 37) % 370), 14.0f).lineHeight(1.4f);
        });
        b.run("8", std::to_string(n) + " paragraphs, width sweep x30", 30,
              [&](int i) { col->width(900.0f - 8.0f * i); }, s);
        if (!interactive) b.clear();
    }

    std::stable_sort(b.results.begin(), b.results.end(), [](const Result& x, const Result& y) { return x.id < y.id; });
    printTables(b.results);
    printBottlenecks(b.results);

    if (interactive) {
        std::printf("\n--interactive: scene kept open (TC2_SHOW_DAMAGE=1 outlines repainted rects). Close the window to exit.\n");
        b.app.run();
    }
    return 0;
}
