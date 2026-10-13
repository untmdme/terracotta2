// Terracotta2 media example: an Image widget (PNG) and an SvgIcon widget (Skia's SVG module),
// both written OUTSIDE the library - only public headers and Skia are used. Also shows how to
// decode on a worker thread and hand the result back to the UI thread through a custom loop.
//
// Build notes (see app/CMakeLists.txt):
//  * SvgIcon needs Skia's SVG module: target_link_libraries(... unofficial::skia::modules::svg)
//  * Images decode through the OS codecs Skia always builds in (ImageIO on macOS, WIC on
//    Windows), so PNG/JPEG/GIF/... need no libpng/libjpeg in vcpkg.json.

#include <terracotta2/Terracotta2.hpp>
#include "include/core/SkColorFilter.h"
#include "include/core/SkData.h"
#include "include/core/SkImage.h"
#include "include/core/SkPaint.h"
#include "include/core/SkRRect.h"
#include "include/core/SkStream.h"
#include "include/core/SkSurface.h"
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
#include <map>
#include <memory>
#include <tuple>
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
        // alignSelf(Align::Stretch) opts back in, for a full-width image.
        alignSelf(Align::Start);
        YGNodeSetMeasureFunc(yogaNode(), &Image::measure);
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

// An SVG is parsed once per file (or inline source) and shared by every SvgIcon showing it. Each
// (pixel size, tint) it is drawn at is rasterized once into a GPU texture, shared too: a list with a
// thousand rows of the same icon parses one file and uploads one small texture, and every repaint
// after that is a textured quad. Sizes are device pixels (screen scale and zoom included), so
// icons stay crisp on any display and inside zoomable canvases.
struct SvgDoc {
    sk_sp<SkSVGDOM> dom;
    SkSize natural{24, 24};
    std::map<std::tuple<int, int, SkColor>, sk_sp<SkImage>> textures; // (width px, height px, tint)
};

std::shared_ptr<SvgDoc> loadSvg(const std::string& key, const std::function<sk_sp<SkSVGDOM>()>& parse) {
    static std::map<std::string, std::shared_ptr<SvgDoc>> cache; // ponytail: never evicted; fine for an app's icon set
    std::shared_ptr<SvgDoc>& doc = cache[key];
    if (doc) return doc;
    doc = std::make_shared<SvgDoc>();
    doc->dom = parse();
    if (doc->dom) {
        // Natural size: width/height attributes, else the viewBox, else a 24x24 icon grid.
        SkSize n = doc->dom->containerSize();
        if (n.isEmpty())
            if (const auto& vb = doc->dom->getRoot()->getViewBox()) n = {vb->width(), vb->height()};
        if (!n.isEmpty()) doc->natural = n;
        doc->dom->setContainerSize(doc->natural);
    }
    return doc;
}

// Renders an SVG file (or SVG source text) scaled to the widget's size, aspect ratio kept.
// tint(color) recolours every painted pixel (monochrome icons); tint(0) keeps the file's own colours.
class SvgIcon : public Widget {
public:
    explicit SvgIcon(const std::string& path = {}, float px = 24) {
        size(px, px);
        if (!path.empty())
            setDoc(loadSvg("file:" + path, [&]() -> sk_sp<SkSVGDOM> {
                SkFILEStream file(path.c_str());
                return file.isValid() ? SkSVGDOM::Builder().make(file) : nullptr;
            }));
    }
    // Inline SVG: add<SvgIcon>().source(R"(<svg viewBox="0 0 24 24">...</svg>)")
    SvgIcon& source(std::string_view svg) {
        const std::string text(svg);
        setDoc(loadSvg("source:" + text, [&] {
            SkMemoryStream mem(text.data(), text.size(), /*copyData=*/false);
            return SkSVGDOM::Builder().make(mem);
        }));
        return *this;
    }
    SvgIcon& tint(SkColor c) { m_tint = c; markPaintDirty(); return *this; }
    bool loaded() const { return m_doc && m_doc->dom; }

    void onDraw(SkCanvas* canvas) override {
        if (!loaded() || m_bounds.isEmpty()) return;
        // Fit the SVG's own coordinate space into our bounds, centred.
        const SkSize n = m_doc->natural;
        const float s = std::min(m_bounds.width() / n.width(), m_bounds.height() / n.height());
        const SkRect dst = SkRect::MakeXYWH(m_bounds.centerX() - n.width() * s / 2, m_bounds.centerY() - n.height() * s / 2,
                                            n.width() * s, n.height() * s);
        // The canvas matrix holds the screen scale and any zoom: rasterize at that many pixels.
        const float dev = canvas->getLocalToDeviceAs3x3().getScaleX();
        const int pw = std::max(1, int(std::ceil(dst.width() * dev))), ph = std::max(1, int(std::ceil(dst.height() * dev)));
        auto& textures = m_doc->textures;
        const auto key = std::make_tuple(pw, ph, m_tint);
        auto it = textures.find(key);
        if (it == textures.end()) {
            // ponytail: a continuous zoom makes one size per step; drop them all once they pile up
            if (textures.size() >= 64) textures.clear();
            it = textures.emplace(key, rasterize(canvas, pw, ph)).first;
        }
        canvas->drawImageRect(it->second.get(), dst, SkSamplingOptions(SkFilterMode::kLinear));
    }

private:
    void setDoc(std::shared_ptr<SvgDoc> doc) {
        m_doc = std::move(doc);
        markPaintDirty();
    }

    sk_sp<SkImage> rasterize(SkCanvas* target, int pw, int ph) const {
        sk_sp<SkSurface> surface = SkSurfaces::Raster(SkImageInfo::MakeN32Premul(pw, ph));
        SkCanvas* c = surface->getCanvas();
        c->scale(pw / m_doc->natural.width(), ph / m_doc->natural.height());
        if (m_tint) {
            // Draw into a layer, then keep the layer's coverage and replace its colour (SrcIn).
            SkPaint layer;
            layer.setColorFilter(SkColorFilters::Blend(m_tint, SkBlendMode::kSrcIn));
            const SkRect area = SkRect::MakeSize(m_doc->natural);
            c->saveLayer(&area, &layer);
        }
        m_doc->dom->render(c);
        if (m_tint) c->restore();
        sk_sp<SkImage> pixels = surface->makeImageSnapshot();
        // Upload once; all windows share one Graphite context, so the texture works in any of them.
        if (skgpu::graphite::Recorder* rec = target->recorder())
            if (sk_sp<SkImage> texture = SkImages::TextureFromImage(rec, pixels.get(), {})) return texture;
        return pixels;
    }

    std::shared_ptr<SvgDoc> m_doc;
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
    Button& b = parent.add<Button>(text).onClick(std::move(onClick)).gap(6);
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
        Window* window = app.createWindow({.title = "Terracotta2 - Images & SVG", .width = 980, .height = 760, .minWidth = 480, .minHeight = 360});
        ScrollView& page = window->root()->add<ScrollView>().padding(20).padding(Edge::Right, 26).gap(16);
        UiQueue ui;
        std::vector<std::jthread> workers; // declared after `app`: joined before the Application dies

        section(page, "Image", "Image(path): natural size by default; width() or height() scales it with the aspect ratio kept.")
            .add<HBox>().gap(16).alignItems(Align::End).build([](HBox& row) {
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
                card.add<HBox>().gap(18).alignItems(Align::Center).build([](HBox& row) {
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
                card.add<Button>("Load on a worker").onClick([&, t = &target, s = &status] {
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
