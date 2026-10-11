// Terracotta2 media example: an Image widget (PNG) and an SvgIcon widget (Skia's SVG module),
// both written OUTSIDE the library - only public headers and Skia are used. Also shows how to
// decode on a worker thread and hand the result back to the UI thread through a custom loop.
//
// Build notes (see app/CMakeLists.txt):
//  * SvgIcon needs Skia's SVG module: target_link_libraries(... unofficial::skia::modules::svg)
//  * Images decode through the OS codecs Skia always builds in (ImageIO on macOS, WIC on
//    Windows), so PNG/JPEG/GIF/... need no libpng/libjpeg in vcpkg.json.
#include <terracotta2/Core/Application.hpp>
#include <terracotta2/Core/Window.hpp>
#include <terracotta2/Theme/Theme.hpp>
#include <terracotta2/UI/Container.hpp>
#include <terracotta2/UI/Controls.hpp>

#include "include/core/SkColorFilter.h"
#include "include/core/SkData.h"
#include "include/core/SkImage.h"
#include "include/core/SkPaint.h"
#include "include/core/SkRRect.h"
#include "include/core/SkStream.h"
#include "include/gpu/graphite/Image.h"
#include "modules/svg/include/SkSVGDOM.h"
#include "modules/svg/include/SkSVGSVG.h"
#if defined(__APPLE__)
#include "include/ports/SkImageGeneratorCG.h"
#elif defined(_WIN32)
#include "include/ports/SkImageGeneratorWIC.h"
#endif

#include <algorithm>
#include <functional>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#ifndef TC2_ASSETS
#define TC2_ASSETS "assets"
#endif

using namespace Terracotta2;

namespace {

// ---------------------------------------------------------------- Image

// Shows a decoded image. A leaf widget sized by a Yoga measure function, exactly like Label:
// its natural size is the image's pixel size (1 image px = 1 logical px), scaled down with the
// aspect ratio kept when the parent offers less room. width()/height() still override it.
class Image : public Widget {
public:
    enum class Fit { Contain, Cover, Fill };

    // Decodes an image file to a CPU (raster) image. Pure Skia, no widget state: safe on any thread.
    static sk_sp<SkImage> decode(const std::string& path) {
        sk_sp<SkData> data = SkData::MakeFromFileName(path.c_str());
        if (!data) return nullptr;
#if defined(__APPLE__)
        auto gen = SkImageGeneratorCG::MakeFromEncodedCG(std::move(data));
#elif defined(_WIN32)
        auto gen = SkImageGeneratorWIC::MakeFromEncodedWIC(std::move(data));
#endif
        sk_sp<SkImage> lazy = gen ? SkImages::DeferredFromGenerator(std::move(gen)) : nullptr;
        return lazy ? lazy->makeRasterImage() : nullptr; // decode now, not on first draw
    }

    explicit Image(sk_sp<SkImage> image = nullptr) {
        // Like Button: don't stretch across a column by default (that would scale the image up).
        // alignSelf(YGAlignStretch) opts back in, for a full-width image.
        alignSelf(YGAlignFlexStart);
        YGNodeSetMeasureFunc(getYGNode(), &Image::measure);
        setImage(std::move(image));
    }
    explicit Image(const std::string& path) : Image(decode(path)) {}

    // Swapping the image may change the natural size: mark layout (re-measure) AND paint dirty.
    Image& setImage(sk_sp<SkImage> image) {
        m_image = std::move(image);
        m_texture.reset();
        markLayoutDirty();
        markPaintDirty();
        return *this;
    }
    Image& fit(Fit f) { m_fit = f; markPaintDirty(); return *this; }
    Image& cornerRadius(float r) { m_radius = r; markPaintDirty(); return *this; }
    bool hasImage() const { return m_image != nullptr; }

    void onDraw(SkCanvas* canvas) override {
        if (!m_image || m_bounds.isEmpty()) return;
        // Graphite draws from GPU textures. Upload once (with mipmaps, for smooth downscaling) and
        // keep the texture: every later repaint is a plain textured quad. All windows share one
        // Graphite context, so the texture also works after the widget moves to another window.
        if (!m_texture)
            if (skgpu::graphite::Recorder* rec = canvas->recorder())
                m_texture = SkImages::TextureFromImage(rec, m_image.get(), {.fMipmapped = true});
        const SkImage* img = m_texture ? m_texture.get() : m_image.get();

        canvas->save();
        canvas->clipRRect(SkRRect::MakeRectXY(m_bounds, m_radius, m_radius), true); // Cover overflows
        canvas->drawImageRect(img, destRect(), SkSamplingOptions(SkFilterMode::kLinear, SkMipmapMode::kLinear));
        canvas->restore();
    }

private:
    // Where the whole image lands inside m_bounds (window coordinates, like m_bounds itself).
    SkRect destRect() const {
        const float iw = float(m_image->width()), ih = float(m_image->height());
        const float sx = m_bounds.width() / iw, sy = m_bounds.height() / ih;
        if (m_fit == Fit::Fill) return m_bounds;
        const float s = m_fit == Fit::Contain ? std::min(sx, sy) : std::max(sx, sy);
        return SkRect::MakeXYWH(m_bounds.centerX() - iw * s / 2, m_bounds.centerY() - ih * s / 2, iw * s, ih * s);
    }

    // Yoga asks "how big would you like to be, given this much room?". Modes: Undefined = no limit,
    // AtMost = at most `w`, Exactly = the size is decided (e.g. by width()), answer the other axis.
    static YGSize measure(YGNodeConstRef node, float w, YGMeasureMode wm, float h, YGMeasureMode hm) {
        auto* self = static_cast<Image*>(YGNodeGetContext(node));
        if (!self->m_image) return {0, 0};
        const float iw = float(self->m_image->width()), ih = float(self->m_image->height());
        float s = 1.0f;
        if (wm == YGMeasureModeExactly) s = w / iw;
        else if (hm == YGMeasureModeExactly) s = h / ih;
        else {
            if (wm == YGMeasureModeAtMost) s = std::min(s, w / iw);
            if (hm == YGMeasureModeAtMost) s = std::min(s, h / ih);
        }
        return {iw * s, ih * s};
    }

    sk_sp<SkImage> m_image;   // CPU pixels (source of truth)
    sk_sp<SkImage> m_texture; // GPU copy, made on first draw
    Fit m_fit = Fit::Contain;
    float m_radius = 0;
};

// ---------------------------------------------------------------- SvgIcon

// Renders an SVG file (or SVG source text) scaled to the widget's size, crisp at any DPI because
// it is drawn as vectors every time. tint(color) recolours every painted pixel (monochrome icons);
// tint(0) keeps the file's own colours.
class SvgIcon : public Widget {
public:
    explicit SvgIcon(const std::string& path = {}, float px = 24) {
        size(px, px);
        if (!path.empty()) {
            SkFILEStream file(path.c_str());
            if (file.isValid()) setDom(SkSVGDOM::Builder().make(file));
        }
    }
    // Inline SVG: add<SvgIcon>().source(R"(<svg viewBox="0 0 24 24">...</svg>)")
    SvgIcon& source(std::string_view svg) {
        SkMemoryStream mem(svg.data(), svg.size(), /*copyData=*/false);
        setDom(SkSVGDOM::Builder().make(mem));
        return *this;
    }
    SvgIcon& tint(SkColor c) { m_tint = c; markPaintDirty(); return *this; }
    bool loaded() const { return m_dom != nullptr; }

    void onDraw(SkCanvas* canvas) override {
        if (!m_dom || m_bounds.isEmpty()) return;
        // Fit the SVG's own coordinate space into our bounds, centred, aspect ratio kept.
        const float s = std::min(m_bounds.width() / m_natural.width(), m_bounds.height() / m_natural.height());
        canvas->save();
        canvas->translate(m_bounds.centerX() - m_natural.width() * s / 2, m_bounds.centerY() - m_natural.height() * s / 2);
        canvas->scale(s, s);
        if (m_tint) {
            // Draw into a layer, then keep the layer's coverage and replace its colour (SrcIn).
            SkPaint layer;
            layer.setColorFilter(SkColorFilters::Blend(m_tint, SkBlendMode::kSrcIn));
            const SkRect area = SkRect::MakeSize(m_natural);
            canvas->saveLayer(&area, &layer);
        }
        m_dom->render(canvas);
        if (m_tint) canvas->restore();
        canvas->restore();
    }

private:
    void setDom(sk_sp<SkSVGDOM> dom) {
        m_dom = std::move(dom);
        if (m_dom) {
            // Natural size: width/height attributes, else the viewBox, else a 24x24 icon grid.
            m_natural = m_dom->containerSize();
            if (m_natural.isEmpty())
                if (const auto& vb = m_dom->getRoot()->getViewBox()) m_natural = {vb->width(), vb->height()};
            if (m_natural.isEmpty()) m_natural = {24, 24};
            m_dom->setContainerSize(m_natural);
        }
        markPaintDirty();
    }

    sk_sp<SkSVGDOM> m_dom;
    SkSize m_natural{24, 24};
    SkColor m_tint = 0;
};

// ---------------------------------------------------------------- UI-thread queue

// Widgets must only be touched on the UI thread. Workers post() a closure; the main loop runs
// drain() between frames. wake() unblocks pumpOnce() so the closure runs right away.
class UiQueue {
public:
    void post(std::function<void()> fn) {
        {
            std::lock_guard lock(m_mutex);
            m_tasks.push_back(std::move(fn));
        }
        Application::get().wake(); // thread-safe
    }
    void drain() {
        std::vector<std::function<void()>> tasks;
        {
            std::lock_guard lock(m_mutex);
            tasks.swap(m_tasks);
        }
        for (auto& t : tasks) t();
    }

private:
    std::mutex m_mutex;
    std::vector<std::function<void()>> m_tasks;
};

constexpr const char* kAssets = TC2_ASSETS;
std::string asset(const char* name) { return std::string(kAssets) + "/" + name; }

// Icon + text button. A Button is a row whose only child is its Label, so an extra child
// adopted at index 0 lines up to the left of the text.
Button& iconButton(Widget& parent, const char* icon, const char* text, SkColor tint, std::function<void()> onClick) {
    Button& b = parent.add<Button>(text, std::move(onClick)).gap(6);
    auto svg = std::make_unique<SvgIcon>(asset(icon), 16);
    svg->tint(tint);
    b.adopt(std::move(svg), 0);
    return b;
}

Panel& section(Widget& page, const char* title, const char* caption) {
    Panel& card = page.add<Panel>().padding(16).gap(10);
    card.add<Label>(title).fontSize(16).weight(600);
    card.add<Label>(caption).fontSize(12).color(theme().textMuted);
    return card;
}

} // namespace

int main() {
    try {
        Application app;
        Window* window = app.createWindow({.title = "Terracotta2 - Images & SVG", .width = 980, .height = 760});
        ScrollView& page = window->getRootWidget()->add<ScrollView>().padding(20).padding(YGEdgeRight, 26).gap(16);
        UiQueue ui;
        std::vector<std::jthread> workers; // declared after `app`: joined before the Application dies

        section(page, "Image", "Image(path): natural size by default; width() or height() scales it with the aspect ratio kept.")
            .add<HBox>().gap(16).alignItems(YGAlignFlexEnd).build([](HBox& row) {
                row.add<Image>(asset("landscape.png")).width(240).cornerRadius(8);
                row.add<Image>(asset("landscape.png")).height(90);
                row.add<Image>(asset("missing.png")).size(90, 90); // decode failed: draws nothing, layout still works
            });

        section(page, "Fit modes", "fit(Contain | Cover | Fill) inside a fixed 160x120 box.")
            .add<HBox>().gap(16).build([](HBox& row) {
                for (auto [name, fit] : {std::pair{"Contain", Image::Fit::Contain}, std::pair{"Cover", Image::Fit::Cover},
                                         std::pair{"Fill", Image::Fit::Fill}})
                    row.add<VBox>().gap(4).build([&](VBox& col) {
                        col.add<Panel>(theme().input.background).build([&](Panel& frame) {
                            frame.add<Image>(asset("landscape.png")).size(160, 120).fit(fit);
                        });
                        col.add<Label>(name).fontSize(12).color(theme().textMuted);
                    });
            });

        section(page, "SVG icons", "SvgIcon(path, px). tint(color) recolours monochrome icons; tint(0) keeps the file's colours.")
            .build([](Panel& card) {
                card.add<HBox>().gap(18).alignItems(YGAlignCenter).build([](HBox& row) {
                    for (const char* file : {"home.svg", "heart.svg", "settings.svg"})
                        row.add<SvgIcon>(asset(file), 24).tint(theme().label.text);
                    row.add<SvgIcon>(asset("heart.svg"), 24).tint(SkColorSetRGB(230, 90, 100));
                    for (float px : {16.0f, 32.0f, 48.0f}) row.add<SvgIcon>(asset("settings.svg"), px).tint(theme().accent);
                    row.add<SvgIcon>(asset("logo.svg"), 64); // gradient, untinted
                    row.add<SvgIcon>().size(28, 28).tint(SkColorSetRGB(240, 200, 90)).source(
                        R"(<svg viewBox="0 0 24 24"><path d="M12 2l3 7h7l-5.5 4.5 2 7.5L12 16.5 5.5 21l2-7.5L2 9h7z"/></svg>)");
                });
                card.add<HBox>().gap(8).build([](HBox& row) {
                    iconButton(row, "home.svg", "Home", theme().button.text, [] { std::cout << "home\n"; });
                    iconButton(row, "heart.svg", "Favourite", SkColorSetRGB(230, 90, 100), [] { std::cout << "favourite\n"; });
                });
            });

        section(page, "Background loading", "Decode on a worker thread, then post the image to the UI thread (UiQueue + pumpOnce loop).")
            .build([&](Panel& card) {
                Image& target = card.add<Image>().size(240, 150).cornerRadius(8);
                Label& status = card.add<Label>("Not loaded").fontSize(12).color(theme().textMuted);
                card.add<Button>("Load on a worker", [&, t = &target, s = &status] {
                    s->text("Decoding...");
                    workers.emplace_back([&ui, t, s] {
                        sk_sp<SkImage> img = Image::decode(asset("landscape.png")); // worker: Skia only, no widgets
                        std::this_thread::sleep_for(std::chrono::milliseconds(400)); // pretend it was big
                        ui.post([t, s, img] { // UI thread: now widgets may be touched
                            t->setImage(img);
                            s->text(img ? "Loaded on a worker thread" : "Decode failed");
                        });
                    });
                });
            });

        // Custom loop: Application::run() is just `while (pumpOnce(true)) {}`. Draining our queue
        // after each iteration is all it takes to integrate worker threads.
        while (app.pumpOnce(true)) ui.drain();
    } catch (const std::exception& e) {
        std::cerr << "Fatal: " << e.what() << "\n";
        return -1;
    }
    return 0;
}
