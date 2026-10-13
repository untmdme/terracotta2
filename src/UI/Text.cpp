#include "Text.hpp"
#include "terracotta2/Theme/Theme.hpp"
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include "include/core/SkData.h"
#include "include/core/SkFontMgr.h"
#include "include/core/SkTypeface.h"
#include "include/core/SkFontStyle.h"
#include "include/core/SkPaint.h"
#include "modules/skparagraph/include/ParagraphBuilder.h"
#include "modules/skparagraph/include/ParagraphStyle.h"
#include "modules/skparagraph/include/TypefaceFontProvider.h"
#include "modules/skparagraph/include/TextStyle.h"
#include "modules/skunicode/include/SkUnicode_icu.h"
#ifdef __APPLE__
#include "include/ports/SkFontMgr_mac_ct.h"
#elif defined(_WIN32)
#include "include/ports/SkTypeface_win.h"
#endif

namespace Terracotta2 {
namespace detail {

using skia::textlayout::FontCollection;
using skia::textlayout::Paragraph;
using skia::textlayout::ParagraphBuilder;
using skia::textlayout::ParagraphStyle;
using skia::textlayout::TextStyle;

sk_sp<SkFontMgr> platformFontMgr() {
    static sk_sp<SkFontMgr> mgr = [] () -> sk_sp<SkFontMgr> {
#ifdef __APPLE__
        return SkFontMgr_New_CoreText(nullptr);
#elif defined(_WIN32)
        return SkFontMgr_New_DirectWrite();
#else
        return nullptr;
#endif
    }();
    return mgr;
}

// Fonts from loadFont() live in the asset manager; the platform manager stays the default for system fonts.
sk_sp<skia::textlayout::TypefaceFontProvider> assetFonts() {
    static auto p = sk_make_sp<skia::textlayout::TypefaceFontProvider>();
    return p;
}

sk_sp<FontCollection> fonts() {
    static sk_sp<FontCollection> fc = [] {
        auto f = sk_make_sp<FontCollection>();
        f->setAssetFontManager(assetFonts());
        f->setDefaultFontManager(platformFontMgr());
        f->enableFontFallback();
        return f;
    }();
    return fc;
}

sk_sp<SkUnicode> unicode() {
    static sk_sp<SkUnicode> u = SkUnicodes::ICU::Make();
    return u;
}

std::unique_ptr<Paragraph> makeParagraph(const std::string& text, float fontSize, SkColor color,
                                         int weight, float lineHeight,
                                         const std::string& family) {
    TextStyle ts;
    ts.setColor(color);
    ts.setFontSize(fontSize);
    ts.setFontStyle(SkFontStyle(weight, SkFontStyle::kNormal_Width, SkFontStyle::kUpright_Slant));
    if (lineHeight > 0) {
        ts.setHeight(lineHeight);
        ts.setHeightOverride(true);
    }
    ts.setFontFamilies({SkString(family.empty() ? theme().fontFamily : family)});
    ParagraphStyle ps;
    ps.setTextStyle(ts);
    auto b = ParagraphBuilder::make(ps, fonts(), unicode());
    b->pushStyle(ts);
    b->addText(text.data(), text.size());
    return b->Build();
}

SkColor lerpColor(SkColor a, SkColor b, float t) {
    auto l = [t](int x, int y) { return static_cast<U8CPU>(std::lround(x + (y - x) * t)); };
    return SkColorSetARGB(l(SkColorGetA(a), SkColorGetA(b)), l(SkColorGetR(a), SkColorGetR(b)),
                          l(SkColorGetG(a), SkColorGetG(b)), l(SkColorGetB(a), SkColorGetB(b)));
}

// ---- UTF-8 / UTF-16 helpers (skparagraph indexes text in UTF-16 code units) ----

bool isCont(unsigned char c) { return (c & 0xC0) == 0x80; }

size_t prevBoundary(const std::string& s, size_t i) {
    if (i == 0) return 0;
    do --i; while (i > 0 && isCont(s[i]));
    return i;
}

size_t nextBoundary(const std::string& s, size_t i) {
    if (i >= s.size()) return s.size();
    do ++i; while (i < s.size() && isCont(s[i]));
    return i;
}

size_t toUtf16(const std::string& s, size_t byteOff) {
    size_t n = 0;
    for (size_t i = 0; i < byteOff && i < s.size(); ++i) {
        unsigned char c = s[i];
        if (isCont(c)) continue;
        n += c >= 0xF0 ? 2 : 1; // 4-byte sequence = surrogate pair
    }
    return n;
}

size_t toByteOffset(const std::string& s, size_t u16) {
    size_t n = 0, i = 0;
    while (i < s.size() && n < u16) {
        n += static_cast<unsigned char>(s[i]) >= 0xF0 ? 2 : 1;
        i = nextBoundary(s, i);
    }
    return i;
}

std::string encodeUtf8(uint32_t cp) {
    std::string o;
    if (cp < 0x80) {
        o += static_cast<char>(cp);
    } else if (cp < 0x800) {
        o += static_cast<char>(0xC0 | (cp >> 6));
        o += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        o += static_cast<char>(0xE0 | (cp >> 12));
        o += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        o += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        o += static_cast<char>(0xF0 | (cp >> 18));
        o += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        o += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        o += static_cast<char>(0x80 | (cp & 0x3F));
    }
    return o;
}

using skia::textlayout::RectHeightStyle;
using skia::textlayout::RectWidthStyle;

// ---- selection helpers shared by Label and InputText (offsets are UTF-8 bytes) ----

// Point (paragraph-local) -> byte offset of the nearest glyph boundary.
size_t hitOffset(Paragraph& p, const std::string& s, float x, float y) {
    return toByteOffset(s, static_cast<size_t>(std::max(0, p.getGlyphPositionAtCoordinate(x, y).position)));
}

bool isWordByte(unsigned char c) { return std::isalnum(c) || c == '_' || c >= 0x80; }

// Word jumps (Option/Ctrl+arrows): skip the gap, then the word, so the caret lands at the word's
// start going left and its end going right, like macOS text fields.
size_t prevWord(const std::string& s, size_t i) {
    while (i > 0 && !isWordByte(s[i - 1])) i = prevBoundary(s, i);
    while (i > 0 && isWordByte(s[i - 1])) i = prevBoundary(s, i);
    return i;
}

size_t nextWord(const std::string& s, size_t i) {
    while (i < s.size() && !isWordByte(s[i])) i = nextBoundary(s, i);
    while (i < s.size() && isWordByte(s[i])) i = nextBoundary(s, i);
    return i;
}

// Double-click range: the run of word characters under `off`, else the single character there.
std::pair<size_t, size_t> wordRange(const std::string& s, size_t off) {
    if (s.empty()) return {0, 0};
    if (off >= s.size()) off = prevBoundary(s, s.size());
    if (!isWordByte(s[off])) return {off, nextBoundary(s, off)};
    size_t a = off, b = off;
    while (a > 0 && isWordByte(s[a - 1])) --a;
    while (b < s.size() && isWordByte(s[b])) ++b;
    return {a, b};
}

// True when this press (by `owner`) lands within 400 ms / 4 px of its previous press.
bool isDoubleClick(const void* owner, float x, float y) {
    static const void* lastOwner = nullptr;
    static double lastMs = -1e9;
    static float lastX = 0, lastY = 0;
    double now = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
    bool dbl = owner == lastOwner && now - lastMs <= 400 && std::abs(x - lastX) <= 4 && std::abs(y - lastY) <= 4;
    lastOwner = dbl ? nullptr : owner; // a third click starts over
    lastMs = now; lastX = x; lastY = y;
    return dbl;
}

// Fills the highlight for bytes [a, b) (any order) at paragraph origin (ox, oy). Call before painting the text.
void paintSelection(SkCanvas* canvas, Paragraph& p, const std::string& s, size_t a, size_t b, float ox, float oy) {
    if (a > b) std::swap(a, b);
    b = std::min(b, s.size());
    if (a >= b) return;
    SkPaint paint;
    paint.setColor(theme().selection);
    for (const auto& tb : p.getRectsForRange(static_cast<unsigned>(toUtf16(s, a)), static_cast<unsigned>(toUtf16(s, b)),
                                             RectHeightStyle::kMax, RectWidthStyle::kTight))
        canvas->drawRect(tb.rect.makeOffset(ox, oy), paint);
}

// Replaces the selection [min, max) of caret/anchor (or inserts at the caret when empty) with `ins`.
void replaceSelection(std::string& v, size_t& caret, size_t& anchor, const std::string& ins) {
    size_t lo = std::min(caret, anchor), hi = std::max(caret, anchor);
    v.replace(lo, hi - lo, ins);
    caret = anchor = lo + ins.size();
}

} // namespace detail

using namespace detail;

std::string loadFont(const std::string& path) {
    auto mgr = platformFontMgr();
    sk_sp<SkData> data = SkData::MakeFromFileName(path.c_str());
    if (!mgr || !data) return {};
    sk_sp<SkTypeface> tf = mgr->makeFromData(std::move(data));
    if (!tf) return {};
    assetFonts()->registerTypeface(tf);
    SkString name;
    tf->getFamilyName(&name);
    return std::string(name.c_str(), name.size());
}

} // namespace Terracotta2
