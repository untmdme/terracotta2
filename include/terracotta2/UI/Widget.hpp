#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>
#include <yoga/Yoga.h>
#include "include/core/SkCanvas.h"
#include "include/core/SkRect.h"
#include "terracotta2/Core/Animation.hpp"

namespace Terracotta2 {

class Window;

enum class Cursor { Arrow, IBeam, Hand, HResize, VResize, Move };

enum class MouseButton { Left, Right, Middle };

// Modifier bit flags (KeyEvent::mods, MouseEvent::mods).
enum Mod : int { ModShift = 1, ModCtrl = 2, ModAlt = 4, ModSuper = 8 /* Cmd on macOS, Win key on Windows */ };

enum class KeyAction { Press, Release, Repeat };

enum class Key {
    Unknown,
    Left, Right, Up, Down, Home, End, PageUp, PageDown,
    Backspace, Delete, Enter, Escape, Tab, Space,
    A, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
    Num0, Num1, Num2, Num3, Num4, Num5, Num6, Num7, Num8, Num9,
    F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,
};

// Coordinates are logical (pre-DPI-scale) pixels in the receiving widget's space: window-local
// unless an ancestor has a childTransform (then the space its bounds are in, see mapFromWindow).
struct MouseEvent {
    float x = 0, y = 0;
    MouseButton button = MouseButton::Left;
    int mods = 0; // Mod flags
};

struct KeyEvent {
    Key key = Key::Unknown;
    KeyAction action = KeyAction::Press;
    int mods = 0; // Mod flags
};

// Retained widget. Owns its children and a Yoga node mirroring the tree.
//
// Declaring UI: add<T>(args...) constructs a child (args go to T's constructor) and returns T&.
// Style/layout setters are chainable and return the widget's own type, and build(fn) runs fn
// on the widget so nested UI reads like the tree it creates:
//
//   root.add<VBox>().padding(16).gap(8).build([](VBox& col) {
//       col.add<Label>("Title").fontSize(26).weight(700);
//       col.add<HBox>().gap(8).build([](HBox& row) {
//           row.add<Button>("OK", [] { /* ... */ });
//       });
//   });
//
// Invalidation contract:
//  - markLayoutDirty(): style/size changed. Bubbles to root; next frame re-runs Yoga.
//    Widgets whose bounds change during layout are damaged automatically (old + new rect).
//  - markPaintDirty(): visuals changed, geometry did not. Damages getBounds() only; no layout.
//  - invalidateRect(r): damage an arbitrary window-space rect (e.g. overlays, carets).
// Antialiasing spills a faint fringe up to 1px past a shape's edge, so damage rects are grown
// by kPaintOverflow and widgets within that distance of the damage are repainted.
inline constexpr float kPaintOverflow = 1.0f; // logical px

class Widget {
public:
    Widget();
    virtual ~Widget();
    Widget(const Widget&) = delete;
    Widget& operator=(const Widget&) = delete;

    // ---- tree ----
    template <typename T, typename... Args>
    T& add(Args&&... args) {
        return static_cast<T&>(*adopt(std::make_unique<T>(std::forward<Args>(args)...)));
    }
    // Stores a pointer to this widget in `out` and returns *this — keep a handle from inside
    // a chain, e.g. for a callback declared earlier:  page.add<Label>("0").ref(counter);
    template <class Self> Self& ref(this Self& self, Self*& out) { out = &self; return self; }
    // Runs fn(*this) and returns *this: nest child declarations inside a chain.
    template <class Self, class F>
    Self& build(this Self& self, F&& fn) {
        std::forward<F>(fn)(self);
        return self;
    }
    // Inserts child at index (SIZE_MAX = append). Returns raw pointer.
    Widget* adopt(std::unique_ptr<Widget> child, size_t index = SIZE_MAX);
    // Removes child from this widget; Window forgets hover/focus/capture/animations in its subtree.
    std::unique_ptr<Widget> detach(Widget* child);
    void clearChildren();

    Widget* parent() const { return m_parent; }
    const std::vector<std::unique_ptr<Widget>>& children() const { return m_children; }
    size_t indexOf(const Widget* child) const; // SIZE_MAX if absent
    Window* window() const;                    // walks to root; nullptr if detached
    bool isAncestorOf(const Widget* w) const;  // true for w == this too

    YGNodeRef getYGNode() const { return m_yg_node; }
    const SkRect& getBounds() const { return m_bounds; }

    // ---- invalidation ----
    void markLayoutDirty();
    void markPaintDirty();
    void invalidateRect(const SkRect& r);
    bool isLayoutDirty() const { return m_layout_dirty; }

    // ---- flex layout (Yoga). Chainable; each marks layout dirty. ----
    template <class Self> Self& direction(this Self& s, YGFlexDirection v) { YGNodeStyleSetFlexDirection(s.m_yg_node, v); s.markLayoutDirty(); return s; }
    template <class Self> Self& grow(this Self& s, float v) { YGNodeStyleSetFlexGrow(s.m_yg_node, v); s.markLayoutDirty(); return s; }
    template <class Self> Self& shrink(this Self& s, float v) { YGNodeStyleSetFlexShrink(s.m_yg_node, v); s.markLayoutDirty(); return s; }
    template <class Self> Self& basis(this Self& s, float v) { YGNodeStyleSetFlexBasis(s.m_yg_node, v); s.markLayoutDirty(); return s; }
    template <class Self> Self& width(this Self& s, float v) { YGNodeStyleSetWidth(s.m_yg_node, v); s.markLayoutDirty(); return s; }
    template <class Self> Self& height(this Self& s, float v) { YGNodeStyleSetHeight(s.m_yg_node, v); s.markLayoutDirty(); return s; }
    template <class Self> Self& size(this Self& s, float w, float h) { return s.width(w).height(h); }
    template <class Self> Self& widthPercent(this Self& s, float v) { YGNodeStyleSetWidthPercent(s.m_yg_node, v); s.markLayoutDirty(); return s; }
    template <class Self> Self& heightPercent(this Self& s, float v) { YGNodeStyleSetHeightPercent(s.m_yg_node, v); s.markLayoutDirty(); return s; }
    template <class Self> Self& minSize(this Self& s, float w, float h) { YGNodeStyleSetMinWidth(s.m_yg_node, w); YGNodeStyleSetMinHeight(s.m_yg_node, h); s.markLayoutDirty(); return s; }
    template <class Self> Self& maxSize(this Self& s, float w, float h) { YGNodeStyleSetMaxWidth(s.m_yg_node, w); YGNodeStyleSetMaxHeight(s.m_yg_node, h); s.markLayoutDirty(); return s; } // YGUndefined = none
    template <class Self> Self& padding(this Self& s, float v) { YGNodeStyleSetPadding(s.m_yg_node, YGEdgeAll, v); s.markLayoutDirty(); return s; }
    template <class Self> Self& padding(this Self& s, YGEdge e, float v) { YGNodeStyleSetPadding(s.m_yg_node, e, v); s.markLayoutDirty(); return s; }
    template <class Self> Self& margin(this Self& s, float v) { YGNodeStyleSetMargin(s.m_yg_node, YGEdgeAll, v); s.markLayoutDirty(); return s; }
    template <class Self> Self& margin(this Self& s, YGEdge e, float v) { YGNodeStyleSetMargin(s.m_yg_node, e, v); s.markLayoutDirty(); return s; }
    template <class Self> Self& gap(this Self& s, float v) { YGNodeStyleSetGap(s.m_yg_node, YGGutterAll, v); s.markLayoutDirty(); return s; }
    template <class Self> Self& alignItems(this Self& s, YGAlign v) { YGNodeStyleSetAlignItems(s.m_yg_node, v); s.markLayoutDirty(); return s; }
    template <class Self> Self& alignSelf(this Self& s, YGAlign v) { YGNodeStyleSetAlignSelf(s.m_yg_node, v); s.markLayoutDirty(); return s; }
    template <class Self> Self& justify(this Self& s, YGJustify v) { YGNodeStyleSetJustifyContent(s.m_yg_node, v); s.markLayoutDirty(); return s; }
    template <class Self> Self& wrap(this Self& s, YGWrap v) { YGNodeStyleSetFlexWrap(s.m_yg_node, v); s.markLayoutDirty(); return s; }
    // Out of flow: positioned at (x, y) inside the parent's padding box, still sized by Yoga.
    template <class Self> Self& absolute(this Self& s, float x, float y) {
        YGNodeStyleSetPositionType(s.m_yg_node, YGPositionTypeAbsolute);
        YGNodeStyleSetPosition(s.m_yg_node, YGEdgeLeft, x);
        YGNodeStyleSetPosition(s.m_yg_node, YGEdgeTop, y);
        s.markLayoutDirty();
        return s;
    }
    // Hidden widgets take no space (YGDisplayNone), are not painted and not hit.
    template <class Self> Self& visible(this Self& s, bool v) { s.setVisible(v); return s; }
    template <class Self> Self& clipChildren(this Self& s, bool v) { s.m_clip_children = v; return s; }
    bool isVisible() const { return m_visible; }
    bool clipsChildren() const { return m_clip_children; }

    // ---- manual layout (opt out of Yoga for this widget's children) ----
    // In manual mode the children are NOT flex items: the widget places each one with
    // child.setFrame(rect relative to this widget), typically from onLayout(). Each child
    // still lays out its own subtree with Yoga inside that frame. Use for virtualized lists,
    // node graphs, timelines, free-form canvases. Switch modes before adding children.
    template <class Self> Self& manualLayout(this Self& s, bool on = true) { s.setManualLayout(on); return s; }
    bool isManualLayout() const { return m_manual; }
    void setFrame(const SkRect& rectInParent); // only for children of a manual-layout parent
    const SkRect& frame() const { return m_frame; }

    // ---- child transform (zoomable canvases) ----
    // Gives the children their own coordinate space, drawn scaled by `scale` with the child-space
    // point `origin` at this widget's top-left:  window = bounds.topLeft + scale * (child - origin).
    // Children's bounds, mouse events and damage rects are all in that space, so widgets inside need
    // no changes. Changing the transform repaints this widget but never re-runs layout, so zoom and
    // pan are cheap. Hit testing and damage are clipped to this widget. Use for node editors, maps,
    // zoomable documents. (childOffset() still applies, inside the space.)
    template <class Self> Self& childTransform(this Self& s, float scale, SkPoint origin) { s.setChildTransform(scale, origin); return s; }
    bool hasChildSpace() const { return m_space; }
    float childScale() const { return m_scale; }
    SkPoint childOrigin() const { return m_origin; }
    // This widget's coordinates (its bounds and events) <-> window coordinates. Identity unless an
    // ancestor has a childTransform. Use mapToWindow(getBounds()) to place a popup next to a widget.
    SkPoint mapToWindow(SkPoint p) const;
    SkRect mapToWindow(const SkRect& r) const;
    SkPoint mapFromWindow(SkPoint p) const;

    // ---- animation (one active tween per widget; starting a new one replaces it) ----
    // Safe to call before the widget is attached: it starts ticking once added to a window.
    // Loop by calling animate() again from onComplete.
    void animate(float durationSec, std::function<void(float)> onUpdate, std::function<void()> onComplete = nullptr,
                 Ease ease = Ease::Smooth);
    bool isAnimating() const { return m_anim.isActive(); }
    bool tickAnimation(float dt) { return m_anim.update(dt); } // called by Window

    // ---- input (return true = handled, stop bubbling to parent) ----
    // The widget that handles onMouseDown captures the mouse: it receives all
    // onMouseMove/onMouseUp until the button is released.
    // Click = press and release over the same widget (button any). Delivered after onMouseUp,
    // bubbling from the deepest widget under both press and release points. No capture needed.
    virtual bool onClick(const MouseEvent&) { return false; }
    bool isHovered() const; // the pointer is over this widget or one of its descendants
    virtual bool onMouseDown(const MouseEvent&) { return false; }
    virtual bool onMouseUp(const MouseEvent&) { return false; }
    virtual bool onMouseMove(const MouseEvent&) { return false; }
    virtual void onMouseEnter() {}
    virtual void onMouseLeave() {}
    virtual bool onScroll(float /*dx*/, float /*dy*/) { return false; }
    virtual bool onKey(const KeyEvent&) { return false; }
    virtual bool onChar(uint32_t /*codepoint*/) { return false; }
    virtual void onFocus() {}
    virtual void onBlur() {}
    virtual bool isFocusable() const { return false; }
    virtual Cursor cursor() const { return Cursor::Arrow; }
    // Deepest visible widget containing (x, y), children tested last-to-first. nullptr if outside.
    virtual Widget* hitTest(float x, float y);

    // ---- painting ----
    virtual void onDraw(SkCanvas*) {}     // before children
    virtual void onDrawOver(SkCanvas*) {} // after children (overlays, drop previews)
    // Paints this subtree, skipping widgets whose bounds miss `damage`. Called by Window.
    void paint(SkCanvas* canvas, const SkRect& damage, int& paintedCount);

    // ---- layout ----
    // After YGNodeCalculateLayout: converts Yoga's parent-relative layout to absolute bounds,
    // descending only into nodes with new layout. Damages old+new bounds on change.
    void applyLayout(float parentX, float parentY);
    virtual void onLayout() {} // bounds changed
    // applyTheme() calls this on every widget: re-apply theme values copied at construction
    // (Yoga sizes/paddings, cached text). Values read in onDraw need nothing.
    virtual void onThemeChanged() {}

protected:
    void setVisible(bool visible);
    void setManualLayout(bool on);
    void setChildTransform(float scale, SkPoint origin);
    // Offset applied to all children's absolute positions (e.g. ScrollView: {0, -scrollY}).
    virtual SkPoint childOffset() const { return {0, 0}; }
    // Call when childOffset() changes: the next applyLayout re-walks this widget's children
    // even though Yoga reports no new layout. Damages are produced as children move.
    void relayoutChildren();

    SkRect m_bounds = SkRect::MakeEmpty();
    Animation m_anim;

private:
    friend class Window;
    Widget* m_parent = nullptr;
    Window* m_host = nullptr; // set on the root widget only
    std::vector<std::unique_ptr<Widget>> m_children;
    YGNodeRef m_yg_node;
    bool m_layout_dirty = true;
    bool m_visible = true;
    bool m_clip_children = true;
    bool m_force_child_layout = false;
    bool m_manual = false;                   // children placed via setFrame, not Yoga
    SkRect m_frame = SkRect::MakeEmpty();    // set by a manual-layout parent
    bool m_space = false;                    // children live in their own (scaled) coordinate space
    float m_scale = 1.0f;
    SkPoint m_origin{0, 0};
    SkPoint toChildSpace(SkPoint p) const { return {(p.x() - m_bounds.left()) / m_scale + m_origin.x(), (p.y() - m_bounds.top()) / m_scale + m_origin.y()}; }
    SkPoint fromChildSpace(SkPoint p) const { return {m_bounds.left() + (p.x() - m_origin.x()) * m_scale, m_bounds.top() + (p.y() - m_origin.y()) * m_scale}; }
};

} // namespace Terracotta2
