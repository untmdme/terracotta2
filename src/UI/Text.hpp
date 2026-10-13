#pragma once
// Internal: text shaping and editing helpers shared by Label, InputText and the other text widgets.
#include <memory>
#include <string>
#include <utility>
#include "include/core/SkCanvas.h"
#include "include/core/SkColor.h"
#include "include/core/SkFontMgr.h"
#include "modules/skparagraph/include/FontCollection.h"
#include "modules/skparagraph/include/Paragraph.h"

namespace Terracotta2::detail {

using skia::textlayout::FontCollection;
using skia::textlayout::Paragraph;
using skia::textlayout::RectHeightStyle;
using skia::textlayout::RectWidthStyle;

sk_sp<SkFontMgr> platformFontMgr(); // system fonts (DirectWrite / CoreText)
sk_sp<FontCollection> fonts();
// Shaped single-style text; call layout(width) on the result before painting it.
std::unique_ptr<Paragraph> makeParagraph(const std::string& text, float fontSize, SkColor color,
                                         int weight = 400, float lineHeight = 0.0f, const std::string& family = {});
SkColor lerpColor(SkColor a, SkColor b, float t);

// ---- UTF-8 helpers (offsets are UTF-8 bytes; skparagraph indexes UTF-16 code units) ----
size_t prevBoundary(const std::string& s, size_t i);
size_t nextBoundary(const std::string& s, size_t i);
size_t toUtf16(const std::string& s, size_t byteOff);
size_t toByteOffset(const std::string& s, size_t u16);
std::string encodeUtf8(uint32_t cp);

// ---- selection helpers shared by Label and InputText ----
size_t hitOffset(Paragraph& p, const std::string& s, float x, float y); // paragraph-local point -> byte offset
std::pair<size_t, size_t> wordRange(const std::string& s, size_t off);  // double-click range
size_t prevWord(const std::string& s, size_t i); // start of the word before i
size_t nextWord(const std::string& s, size_t i); // end of the word after i
bool isDoubleClick(const void* owner, float x, float y);
void paintSelection(SkCanvas* canvas, Paragraph& p, const std::string& s, size_t a, size_t b, float ox, float oy);
void replaceSelection(std::string& v, size_t& caret, size_t& anchor, const std::string& ins);

} // namespace Terracotta2::detail
