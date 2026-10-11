#include "terracotta2/UI/Controls.hpp"
#include "terracotta2/Core/Application.hpp"
#include "terracotta2/Core/Window.hpp"
#include "terracotta2/Theme/Theme.hpp"
#include <algorithm>
#include <array>
#include <cstdio>
#include <vector>
#include <cctype>
#include <chrono>
#include <cmath>
#include <tuple>
#include "include/core/SkData.h"
#include "include/core/SkFontMgr.h"
#include "include/core/SkTypeface.h"
#include "include/core/SkFontStyle.h"
#include "include/core/SkPaint.h"
#include "include/core/SkRRect.h"
#include "include/effects/SkGradient.h"
#include "modules/skparagraph/include/FontCollection.h"
#include "modules/skparagraph/include/Paragraph.h"
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
namespace {

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
                                         int weight = 400, float lineHeight = 0.0f,
                                         const std::string& family = {}) {
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

YGSize measureLabel(YGNodeConstRef node, float width, YGMeasureMode wm, float, YGMeasureMode) {
    auto* label = static_cast<Label*>(YGNodeGetContext(node));
    SkSize s = label->measure(wm == YGMeasureModeUndefined ? SK_ScalarInfinity : width);
    return {s.width(), s.height()};
}

} // namespace

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

// ================= Label =================

Label::Label(std::string text, float fontSize, int weight)
    : m_text(std::move(text)),
      m_fontSize(fontSize > 0 ? fontSize : theme().label.fontSize),
      m_weight(weight),
      m_color(theme().label.text),
      m_themeSize(fontSize <= 0) {
    YGNodeSetMeasureFunc(getYGNode(), measureLabel);
}

void Label::onThemeChanged() {
    if (m_themeSize) m_fontSize = theme().label.fontSize;
    if (m_themeColor) m_color = theme().label.text;
    m_paragraph.reset(); // theme().fontFamily may have changed too
    markLayoutDirty();
}

Label::~Label() = default;

Label& Label::text(std::string text) {
    m_text = std::move(text);
    m_selAnchor = m_selEnd = 0;
    m_paragraph.reset();
    markLayoutDirty();
    markPaintDirty(); // size may be unchanged but content did change
    return *this;
}

Label& Label::color(SkColor color) {
    m_color = color;
    m_themeColor = false;
    m_paragraph.reset();
    markPaintDirty();
    return *this;
}

Label& Label::fontSize(float size) {
    m_fontSize = size > 0 ? size : theme().label.fontSize;
    m_themeSize = size <= 0;
    m_paragraph.reset();
    markLayoutDirty();
    markPaintDirty();
    return *this;
}

Label& Label::weight(int weight) {
    m_weight = weight;
    m_paragraph.reset();
    markLayoutDirty();
    markPaintDirty();
    return *this;
}

Label& Label::lineHeight(float multiplier) {
    m_lineHeight = multiplier;
    m_paragraph.reset();
    markLayoutDirty();
    markPaintDirty();
    return *this;
}

Label& Label::font(std::string family) {
    m_family = std::move(family);
    m_paragraph.reset();
    markLayoutDirty();
    markPaintDirty();
    return *this;
}

SkSize Label::measure(float maxWidth) {
    if (!m_paragraph) {
        m_paragraph = makeParagraph(m_text, m_fontSize, m_color, m_weight, m_lineHeight, m_family);
        m_layoutWidth = -1.0f;
    }
    if (m_layoutWidth != maxWidth) {
        m_paragraph->layout(maxWidth);
        m_layoutWidth = maxWidth;
    }
    float intrinsic = std::ceil(m_paragraph->getMaxIntrinsicWidth());
    float w = std::isinf(maxWidth) ? intrinsic : std::min(maxWidth, intrinsic);
    return SkSize::Make(w, std::ceil(m_paragraph->getHeight()));
}

void Label::onDraw(SkCanvas* canvas) {
    if (!m_paragraph) {
        m_paragraph = makeParagraph(m_text, m_fontSize, m_color, m_weight, m_lineHeight, m_family);
        m_layoutWidth = -1.0f;
    }
    if (m_layoutWidth != m_bounds.width()) {
        m_paragraph->layout(m_bounds.width());
        m_layoutWidth = m_bounds.width();
    }
    if (m_selAnchor != m_selEnd)
        paintSelection(canvas, *m_paragraph, m_text, m_selAnchor, m_selEnd, m_bounds.left(), m_bounds.top());
    m_paragraph->paint(canvas, m_bounds.left(), m_bounds.top());
}

Label& Label::selectable(bool on) {
    if (m_selectable == on) return *this;
    m_selectable = on;
    m_selecting = false;
    if (!on && m_selAnchor != m_selEnd) {
        m_selAnchor = m_selEnd = 0;
        markPaintDirty();
    }
    return *this;
}

std::string Label::selectedText() const {
    size_t lo = std::min(m_selAnchor, m_selEnd), hi = std::min(std::max(m_selAnchor, m_selEnd), m_text.size());
    return lo < hi ? m_text.substr(lo, hi - lo) : std::string();
}

bool Label::onMouseDown(const MouseEvent& e) {
    if (!m_selectable || e.button != MouseButton::Left) return false; // bubble to the parent as before
    measure(m_bounds.width()); // makes sure the paragraph exists and is laid out for hit-testing
    size_t off = hitOffset(*m_paragraph, m_text, e.x - m_bounds.left(), e.y - m_bounds.top());
    size_t a = off, b = off;
    if (isDoubleClick(this, e.x, e.y)) std::tie(a, b) = wordRange(m_text, off);
    if (a != m_selAnchor || b != m_selEnd) {
        m_selAnchor = a;
        m_selEnd = b;
        markPaintDirty();
    }
    m_selecting = true;
    return true;
}

bool Label::onMouseMove(const MouseEvent& e) {
    if (!m_selecting) return false;
    size_t off = hitOffset(*m_paragraph, m_text, e.x - m_bounds.left(), e.y - m_bounds.top());
    if (off != m_selEnd) {
        m_selEnd = off;
        markPaintDirty();
    }
    return true;
}

bool Label::onMouseUp(const MouseEvent&) {
    bool was = m_selecting;
    m_selecting = false;
    return was;
}

bool Label::onKey(const KeyEvent& e) {
    if (!m_selectable || e.action == KeyAction::Release || !(e.mods & (ModSuper | ModCtrl))) return false;
    if (e.key == Key::A) {
        if (m_selAnchor != 0 || m_selEnd != m_text.size()) {
            m_selAnchor = 0;
            m_selEnd = m_text.size();
            markPaintDirty();
        }
        return true;
    }
    if (e.key == Key::C) {
        std::string sel = selectedText();
        if (!sel.empty()) Application::get().setClipboard(sel);
        return true;
    }
    return false;
}

void Label::onBlur() {
    m_selecting = false;
    if (m_selAnchor != m_selEnd) {
        m_selAnchor = m_selEnd = 0;
        markPaintDirty();
    }
}

// ================= Button =================

Button::Button(std::string text, std::function<void()> onClick) : m_onClick(std::move(onClick)) {
    const auto& st = theme().button;
    direction(YGFlexDirectionRow).alignItems(YGAlignCenter)
        .padding(YGEdgeHorizontal, st.paddingX).padding(YGEdgeVertical, st.paddingY)
        .alignSelf(YGAlignFlexStart);
    m_label = &add<Label>(std::move(text)).color(st.text);
}

void Button::onThemeChanged() {
    const auto& st = theme().button;
    padding(YGEdgeHorizontal, st.paddingX).padding(YGEdgeVertical, st.paddingY);
    m_label->color(st.text);
}

bool Button::onMouseDown(const MouseEvent& e) {
    if (e.button != MouseButton::Left) return false;
    m_pressed = true;
    markPaintDirty();
    return true;
}

bool Button::onMouseUp(const MouseEvent&) {
    if (!m_pressed) return false;
    m_pressed = false;
    markPaintDirty();
    return true;
}

bool Button::onClick(const MouseEvent& e) {
    if (e.button != MouseButton::Left) return false;
    if (m_onClick) m_onClick(); // last: handler may destroy us
    return true;
}

void Button::onMouseEnter() {
    float from = m_hover;
    animate(theme().button.hoverFadeSec, [this, from](float t) {
        m_hover = from + (1.0f - from) * t;
        markPaintDirty();
    });
}

void Button::onMouseLeave() {
    float from = m_hover;
    animate(theme().button.hoverFadeSec, [this, from](float t) {
        m_hover = from * (1.0f - t);
        markPaintDirty();
    });
}

void Button::onDraw(SkCanvas* canvas) {
    const auto& st = theme().button;
    SkPaint p;
    p.setAntiAlias(true);
    p.setColor(m_pressed ? st.activeBackground : lerpColor(st.background, st.hoverBackground, m_hover));
    canvas->drawRoundRect(m_bounds, st.cornerRadius, st.cornerRadius, p);
}

// ================= InputText =================

InputText::InputText(std::string placeholder, std::function<void(const std::string&)> onChange)
    : m_placeholder(std::move(placeholder)), m_onChange(std::move(onChange)) {
    height(theme().input.height).padding(YGEdgeHorizontal, theme().input.paddingX).shrink(0.0f);
}

InputText::~InputText() = default;

void InputText::onThemeChanged() {
    height(theme().input.height).padding(YGEdgeHorizontal, theme().input.paddingX);
    m_paragraph.reset();
}

namespace {
// Single line: laid out at infinite width so it never wraps; the draw clip handles overflow.
Paragraph& inputParagraph(std::unique_ptr<Paragraph>& slot, const std::string& value, const std::string& placeholder) {
    if (!slot) {
        const auto& st = theme();
        bool empty = value.empty();
        slot = makeParagraph(empty ? placeholder : value, st.label.fontSize,
                             empty ? st.input.placeholder : st.input.text);
        slot->layout(SK_ScalarInfinity);
    }
    return *slot;
}
} // namespace

void InputText::setValue(std::string v) {
    m_value = std::move(v);
    m_caret = m_anchor = m_value.size();
    m_paragraph.reset();
    markPaintDirty();
}

void InputText::edited() {
    m_paragraph.reset();
    markPaintDirty();
    if (m_onChange) m_onChange(m_value);
}

namespace {
size_t inputHit(std::unique_ptr<Paragraph>& slot, const std::string& value, const std::string& placeholder,
                const SkRect& bounds, float mouseX) {
    if (value.empty()) return 0;
    Paragraph& p = inputParagraph(slot, value, placeholder);
    return hitOffset(p, value, mouseX - (bounds.left() + theme().input.paddingX), p.getHeight() * 0.5f);
}
} // namespace

bool InputText::onMouseDown(const MouseEvent& e) {
    if (e.button != MouseButton::Left) return false;
    size_t off = inputHit(m_paragraph, m_value, m_placeholder, m_bounds, e.x);
    size_t anchor = m_anchor, caret = off;
    if (isDoubleClick(this, e.x, e.y)) std::tie(anchor, caret) = wordRange(m_value, off);
    else if (!(e.mods & ModShift)) anchor = off;
    if (anchor != m_anchor || caret != m_caret) {
        m_anchor = anchor;
        m_caret = caret;
        markPaintDirty();
    }
    m_selecting = true;
    return true;
}

bool InputText::onMouseMove(const MouseEvent& e) {
    if (!m_selecting) return false;
    size_t off = inputHit(m_paragraph, m_value, m_placeholder, m_bounds, e.x);
    if (off != m_caret) {
        m_caret = off;
        markPaintDirty();
    }
    return true;
}

bool InputText::onMouseUp(const MouseEvent&) {
    bool was = m_selecting;
    m_selecting = false;
    return was;
}

bool InputText::onChar(uint32_t cp) {
    if (cp < 32 || cp == 127) return false;
    replaceSelection(m_value, m_caret, m_anchor, encodeUtf8(cp));
    edited();
    return true;
}

bool InputText::onKey(const KeyEvent& e) {
    if (e.action == KeyAction::Release) return false;
    const bool shift = e.mods & ModShift, cmd = e.mods & (ModSuper | ModCtrl);
    const size_t lo = std::min(m_caret, m_anchor), hi = std::max(m_caret, m_anchor);
    // Moves the caret (extending the selection with Shift) and repaints only if something changed.
    auto moveTo = [&](size_t pos) {
        size_t anchor = shift ? m_anchor : pos;
        if (pos != m_caret || anchor != m_anchor) {
            m_caret = pos;
            m_anchor = anchor;
            markPaintDirty();
        }
        return true;
    };
    auto copy = [&] { if (lo != hi) Application::get().setClipboard(m_value.substr(lo, hi - lo)); };
    switch (e.key) {
    case Key::Left:  return (lo != hi && !shift) ? moveTo(lo) : moveTo(prevBoundary(m_value, m_caret));
    case Key::Right: return (lo != hi && !shift) ? moveTo(hi) : moveTo(nextBoundary(m_value, m_caret));
    case Key::Home:  return moveTo(0);
    case Key::End:   return moveTo(m_value.size());
    case Key::Backspace:
        if (lo == hi) m_anchor = prevBoundary(m_value, m_caret);
        if (m_anchor == m_caret) return true; // nothing before the caret
        replaceSelection(m_value, m_caret, m_anchor, "");
        edited();
        return true;
    case Key::Delete:
        if (lo == hi) m_anchor = nextBoundary(m_value, m_caret);
        if (m_anchor == m_caret) return true;
        replaceSelection(m_value, m_caret, m_anchor, "");
        edited();
        return true;
    case Key::A:
        if (!cmd) return false;
        if (m_anchor != 0 || m_caret != m_value.size()) {
            m_anchor = 0;
            m_caret = m_value.size();
            markPaintDirty();
        }
        return true;
    case Key::C:
        if (!cmd) return false;
        copy();
        return true;
    case Key::X:
        if (!cmd) return false;
        if (lo == hi) return true;
        copy();
        replaceSelection(m_value, m_caret, m_anchor, "");
        edited();
        return true;
    case Key::V:
        if (cmd) {
            std::string clip = Application::get().clipboard();
            clip.erase(std::remove_if(clip.begin(), clip.end(), [](char c) { return c == '\n' || c == '\r'; }), clip.end());
            if (clip.empty()) return true;
            replaceSelection(m_value, m_caret, m_anchor, clip);
            edited();
            return true;
        }
        return false;
    case Key::Escape:
        if (Window* w = window()) w->setFocus(nullptr);
        return true;
    default: return false;
    }
}

void InputText::onFocus() { m_focused = true; markPaintDirty(); }
void InputText::onBlur() {
    m_focused = false;
    m_selecting = false;
    m_anchor = m_caret;
    markPaintDirty();
}

void InputText::onDraw(SkCanvas* canvas) {
    const auto& st = theme().input;
    SkPaint p;
    p.setAntiAlias(true);
    p.setColor(st.background);
    canvas->drawRoundRect(m_bounds, st.cornerRadius, st.cornerRadius, p);
    p.setStyle(SkPaint::kStroke_Style);
    p.setStrokeWidth(1.0f);
    p.setColor(m_focused ? st.focusBorder : st.border);
    canvas->drawRoundRect(m_bounds.makeInset(0.5f, 0.5f), st.cornerRadius, st.cornerRadius, p);

    Paragraph& para = inputParagraph(m_paragraph, m_value, m_placeholder);
    const float lineH = para.getHeight() > 0 ? para.getHeight() : theme().label.fontSize * 1.2f;
    const float x = m_bounds.left() + st.paddingX;
    const float y = m_bounds.top() + (m_bounds.height() - lineH) * 0.5f;

    canvas->save();
    // ponytail: long text simply clips; horizontal scrolling to keep the caret visible is the upgrade path
    canvas->clipRect(m_bounds.makeInset(st.paddingX, 1.0f));
    if (m_focused && !m_value.empty()) paintSelection(canvas, para, m_value, m_anchor, m_caret, x, y);
    para.paint(canvas, x, y);
    if (m_focused) {
        float cx = 0;
        if (m_caret > 0) {
            auto boxes = para.getRectsForRange(static_cast<unsigned>(toUtf16(m_value, prevBoundary(m_value, m_caret))),
                                               static_cast<unsigned>(toUtf16(m_value, m_caret)),
                                               skia::textlayout::RectHeightStyle::kTight,
                                               skia::textlayout::RectWidthStyle::kTight);
            if (!boxes.empty()) cx = boxes.back().rect.fRight;
        }
        SkPaint cp;
        cp.setColor(st.caret);
        canvas->drawRect(SkRect::MakeXYWH(std::floor(x + cx), y, 1.0f, lineH), cp);
    }
    // ponytail: no caret blink (zero idle frames); blink via animate() if wanted
    canvas->restore();
}

// ================= DragNumber =================

DragNumber::DragNumber(std::function<float()> get, std::function<void(float)> set, float min, float max,
                       float perPixel, int decimals)
    : m_get(std::move(get)), m_set(std::move(set)), m_min(min), m_max(max), m_perPixel(perPixel), m_decimals(decimals) {
    height(theme().input.height).justify(YGJustifyCenter).alignItems(YGAlignCenter).shrink(0.0f);
    m_label = &add<Label>("");
    refresh();
}

void DragNumber::refresh() {
    const float v = m_get();
    char buf[32];
    if (std::isnan(v)) std::snprintf(buf, sizeof buf, "auto");
    else std::snprintf(buf, sizeof buf, "%.*f", m_decimals, v);
    if (m_label->text() != buf) m_label->text(buf);
    markPaintDirty(); // the fill
}

bool DragNumber::onMouseDown(const MouseEvent& e) {
    if (e.button != MouseButton::Left) return false;
    m_drag = true;
    m_grabX = e.x;
    m_grabValue = !std::isnan(m_get()) ? m_get() : m_auto ? m_auto() : 0.0f;
    return true;
}

bool DragNumber::onMouseMove(const MouseEvent& e) {
    if (!m_drag) return false;
    m_set(std::clamp(m_grabValue + (e.x - m_grabX) * m_perPixel, m_min, m_max));
    refresh();
    return true;
}

bool DragNumber::onMouseUp(const MouseEvent&) {
    m_drag = false;
    return true;
}

void DragNumber::onDraw(SkCanvas* canvas) {
    const auto& st = theme().input;
    SkPaint p;
    p.setAntiAlias(true);
    p.setColor(st.background);
    canvas->drawRoundRect(m_bounds, st.cornerRadius, st.cornerRadius, p);
    const float v = m_get();
    if (!std::isnan(v) && m_max > m_min) {
        canvas->save();
        canvas->clipRRect(SkRRect::MakeRectXY(m_bounds, st.cornerRadius, st.cornerRadius), true);
        SkRect fill = m_bounds;
        fill.fRight = fill.fLeft + fill.width() * std::clamp((v - m_min) / (m_max - m_min), 0.0f, 1.0f);
        p.setColor(SkColorSetA(theme().accent, 90));
        canvas->drawRect(fill, p);
        canvas->restore();
    }
}

// ================= ColorPicker =================

namespace {
constexpr float kPickW = 240, kSVH = 150, kBar = 16, kPickGap = 8;

void drawChecker(SkCanvas* c, const SkRect& r) {
    SkPaint p;
    p.setColor(SkColorSetRGB(200, 200, 200));
    c->drawRect(r, p);
    p.setColor(SkColorSetRGB(150, 150, 150));
    const float s = 6;
    for (float y = r.top(); y < r.bottom(); y += s)
        for (float x = r.left() + (int((y - r.top()) / s) % 2) * s; x < r.right(); x += 2 * s)
            c->drawRect(SkRect::MakeLTRB(x, y, std::min(x + s, r.right()), std::min(y + s, r.bottom())), p);
}

sk_sp<SkShader> linear(SkPoint a, SkPoint b, std::initializer_list<SkColor> colors) {
    std::vector<SkColor4f> stops;
    for (SkColor c : colors) stops.push_back(SkColor4f::FromColor(c));
    SkPoint pts[] = {a, b};
    return SkShaders::LinearGradient(
        pts, SkGradient(SkGradient::Colors({stops.data(), stops.size()}, SkTileMode::kClamp), SkGradient::Interpolation()));
}
} // namespace

std::string colorToHex(SkColor c) {
    char buf[16];
    if (SkColorGetA(c) == 255) std::snprintf(buf, sizeof buf, "#%02X%02X%02X", SkColorGetR(c), SkColorGetG(c), SkColorGetB(c));
    else std::snprintf(buf, sizeof buf, "#%02X%02X%02X%02X", SkColorGetR(c), SkColorGetG(c), SkColorGetB(c), SkColorGetA(c));
    return buf;
}

bool hexToColor(const std::string& hex, SkColor& out) {
    std::string h = hex.starts_with('#') ? hex.substr(1) : hex;
    if (h.size() == 3) h = {h[0], h[0], h[1], h[1], h[2], h[2]};
    if ((h.size() != 6 && h.size() != 8) || !std::all_of(h.begin(), h.end(), [](char ch) { return std::isxdigit(uint8_t(ch)); }))
        return false;
    const uint32_t v = uint32_t(std::stoul(h, nullptr, 16));
    out = h.size() == 6 ? (0xFF000000u | v) : ((v & 0xFF) << 24 | v >> 8); // RRGGBBAA -> ARGB
    return true;
}

ColorPicker::ColorPicker(std::function<SkColor()> get, std::function<void(SkColor)> set)
    : m_get(std::move(get)), m_set(std::move(set)) {
    width(kPickW).padding(YGEdgeTop, kSVH + kBar + 2 * kPickGap).shrink(0.0f); // the hex field sits below the drawn parts
    m_hex = &add<InputText>("#RRGGBB", [this](const std::string& text) {
        SkColor c;
        if (!hexToColor(text, c)) return;
        m_shown = c;
        m_alpha = SkColorGetA(c);
        SkColorToHSV(c, m_hsv);
        m_set(c);
        markPaintDirty();
    });
    refresh();
}

void ColorPicker::refresh() {
    const SkColor c = m_get();
    if (c == m_shown) return;
    m_shown = c;
    m_alpha = SkColorGetA(c);
    float hsv[3];
    SkColorToHSV(c, hsv);
    if (hsv[1] > 0 && hsv[2] > 0) m_hsv[0] = hsv[0]; // grey/black have no hue: keep the old one
    if (hsv[2] > 0) m_hsv[1] = hsv[1];
    m_hsv[2] = hsv[2];
    m_hex->setValue(colorToHex(c));
    markPaintDirty();
}

void ColorPicker::apply() {
    m_shown = SkHSVToColor(m_alpha, m_hsv);
    m_set(m_shown);
    m_hex->setValue(colorToHex(m_shown));
    markPaintDirty();
}

SkRect ColorPicker::svRect() const {
    return SkRect::MakeXYWH(m_bounds.left(), m_bounds.top(), m_bounds.width() - kBar - kPickGap, kSVH);
}
SkRect ColorPicker::hueRect() const { return SkRect::MakeXYWH(m_bounds.right() - kBar, m_bounds.top(), kBar, kSVH); }
SkRect ColorPicker::alphaRect() const {
    return SkRect::MakeXYWH(m_bounds.left(), m_bounds.top() + kSVH + kPickGap, m_bounds.width(), kBar);
}

ColorPicker::Part ColorPicker::partAt(float x, float y) const {
    if (svRect().contains(x, y)) return Part::SV;
    if (hueRect().contains(x, y)) return Part::Hue;
    if (alphaRect().contains(x, y)) return Part::Alpha;
    return Part::None;
}

void ColorPicker::dragTo(float x, float y) {
    auto unit = [](float v, float a, float b) { return std::clamp((v - a) / (b - a), 0.0f, 1.0f); };
    if (m_drag == Part::SV) {
        const SkRect r = svRect();
        m_hsv[1] = unit(x, r.left(), r.right());
        m_hsv[2] = 1.0f - unit(y, r.top(), r.bottom());
    } else if (m_drag == Part::Hue) {
        const SkRect r = hueRect();
        m_hsv[0] = std::min(359.99f, 360.0f * unit(y, r.top(), r.bottom()));
    } else if (m_drag == Part::Alpha) {
        const SkRect r = alphaRect();
        m_alpha = uint8_t(std::lround(255 * unit(x, r.left(), r.right())));
    }
    apply();
}

bool ColorPicker::onMouseDown(const MouseEvent& e) {
    if (e.button != MouseButton::Left || (m_drag = partAt(e.x, e.y)) == Part::None) return false;
    dragTo(e.x, e.y);
    return true;
}
bool ColorPicker::onMouseMove(const MouseEvent& e) {
    if (m_drag == Part::None) return false;
    dragTo(e.x, e.y);
    return true;
}
bool ColorPicker::onMouseUp(const MouseEvent&) {
    m_drag = Part::None;
    return true;
}

void ColorPicker::onDraw(SkCanvas* c) {
    SkPaint p;
    p.setAntiAlias(true);
    // Saturation/value: the pure hue, white fading in from the left, black from the bottom.
    const SkRect sv = svRect();
    p.setColor(SkHSVToColor(255, std::array<float, 3>{m_hsv[0], 1, 1}.data()));
    c->drawRect(sv, p);
    p.setShader(linear({sv.left(), 0}, {sv.right(), 0}, {SK_ColorWHITE, SkColorSetARGB(0, 255, 255, 255)}));
    c->drawRect(sv, p);
    p.setShader(linear({0, sv.top()}, {0, sv.bottom()}, {SkColorSetARGB(0, 0, 0, 0), SK_ColorBLACK}));
    c->drawRect(sv, p);
    // Hue: red -> yellow -> green -> cyan -> blue -> magenta -> red, top to bottom.
    const SkRect hue = hueRect();
    p.setShader(linear({0, hue.top()}, {0, hue.bottom()},
                       {SK_ColorRED, SK_ColorYELLOW, SK_ColorGREEN, SK_ColorCYAN, SK_ColorBLUE, SK_ColorMAGENTA, SK_ColorRED}));
    c->drawRect(hue, p);
    // Alpha: the color from transparent to opaque over a checkerboard.
    const SkRect al = alphaRect();
    drawChecker(c, al);
    const SkColor opaque = SkHSVToColor(255, m_hsv);
    p.setShader(linear({al.left(), 0}, {al.right(), 0}, {SkColorSetA(opaque, 0), opaque}));
    c->drawRect(al, p);
    p.setShader(nullptr);

    // Markers: ring on the SV square, bars across hue and alpha. Clipped: they must not leave our bounds.
    c->save();
    c->clipRect(m_bounds);
    p.setStyle(SkPaint::kStroke_Style);
    p.setStrokeWidth(2);
    p.setColor(SK_ColorWHITE);
    c->drawCircle(sv.left() + m_hsv[1] * sv.width(), sv.top() + (1 - m_hsv[2]) * sv.height(), 5, p);
    const float hy = hue.top() + m_hsv[0] / 360.0f * hue.height();
    c->drawRect(SkRect::MakeLTRB(hue.left() - 1, hy - 2, hue.right() + 1, hy + 2), p);
    const float ax = al.left() + m_alpha / 255.0f * al.width();
    c->drawRect(SkRect::MakeLTRB(ax - 2, al.top() - 1, ax + 2, al.bottom() + 1), p);
    c->restore();
}

} // namespace Terracotta2
