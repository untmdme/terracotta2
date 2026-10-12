// Node editor data model: node types, keyframed parameters, wiring, copy/paste and the
// evaluation that turns the graph into shapes. No UI code, so model_test.cpp can check it alone.
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <optional>
#include <random>
#include <string>
#include <utility>
#include <vector>
#include "include/core/SkColor.h"
#include "include/core/SkPoint.h"

namespace NodeEd {

constexpr float kDuration = 30.0f; // seconds on the timeline
constexpr float kFps = 30.0f;      // keys and scrubbing snap to frames
constexpr float kSceneW = 1920, kSceneH = 1080;

inline float snapFrame(float t) { return std::clamp(std::round(t * kFps) / kFps, 0.0f, kDuration); }

enum class NodeType { Position, Size, Color, Shape, Output };
constexpr int kNodeTypes = 5;
// What flows along a wire. A source node outputs the port with its own index (Position node -> Port::Position).
enum class Port { Position, Size, Color, Shape };

struct ParamDef {
    const char* name;
    float min, max, def, perPixel; // perPixel: drag speed of the number field
    int decimals;
    std::vector<const char*> choices = {}; // non-empty: a dropdown; keys hold the index and do not interpolate
};

struct TypeDef {
    const char* name;
    SkColor color;
    bool hasOutput;
    std::vector<Port> inputs;
    std::vector<ParamDef> params;
};

// Parameter and input indices per node type.
enum { kX, kY };
enum { kWidth, kHeight };
enum { kHue, kSat, kVal, kAlpha };
enum { kKind, kRotation, kStroke };
enum { kBlend, kOpacity };
enum { kInShape, kInPosition, kInSize, kInColor };

inline const std::vector<const char*> kShapeNames = {"Circle", "Rectangle", "Triangle", "Star", "Hexagon"};
inline const std::vector<const char*> kBlendNames = {"Normal", "Add", "Multiply", "Screen"};

inline const TypeDef& typeDef(NodeType t) {
    static const TypeDef defs[] = {
        {"Position", SkColorSetRGB(110, 96, 190), true, {},
         {{"X", 0, kSceneW, kSceneW / 2, 1, 0}, {"Y", 0, kSceneH, kSceneH / 2, 1, 0}}},
        {"Size", SkColorSetRGB(52, 128, 196), true, {},
         {{"Width", 1, 600, 80, 0.5f, 0}, {"Height", 1, 600, 80, 0.5f, 0}}},
        {"Color", SkColorSetRGB(184, 150, 40), true, {},
         {{"Hue", 0, 360, 0, 1, 0}, {"Saturation", 0, 1, 0, 0.005f, 2}, {"Value", 0, 1, 1, 0.005f, 2},
          {"Alpha", 0, 1, 1, 0.005f, 2}}},
        {"Shape", SkColorSetRGB(60, 150, 100), true, {},
         {{"Kind", 0, 4, 0, 0, 0, kShapeNames}, {"Rotation", -360, 360, 0, 1, 0}, {"Stroke", 0, 20, 0, 0.1f, 1}}},
        {"Output", SkColorSetRGB(184, 64, 64), false, {Port::Shape, Port::Position, Port::Size, Port::Color},
         {{"Blend", 0, 3, 0, 0, 0, kBlendNames}, {"Opacity", 0, 1, 1, 0.005f, 2}}},
    };
    return defs[int(t)];
}
inline SkColor portColor(Port p) { return typeDef(NodeType(int(p))).color; }

struct Keyframe {
    float t, v;
};

struct Param {
    float value = 0;             // used while there are no keys
    std::vector<Keyframe> keys;  // sorted by t, at most one per frame

    bool animated() const { return !keys.empty(); }
    // Eased between keys (smoothstep), held before the first and after the last; `stepped` holds
    // each key until the next one (dropdowns).
    float at(float t, bool stepped) const {
        if (keys.empty()) return value;
        if (t <= keys.front().t) return keys.front().v;
        if (t >= keys.back().t) return keys.back().v;
        auto b = std::upper_bound(keys.begin(), keys.end(), t, [](float x, const Keyframe& k) { return x < k.t; });
        auto a = b - 1;
        if (stepped) return a->v;
        float u = (t - a->t) / (b->t - a->t);
        u = u * u * (3 - 2 * u);
        return a->v + (b->v - a->v) * u;
    }
    const Keyframe* keyAt(float t) const {
        for (const Keyframe& k : keys)
            if (std::abs(k.t - t) < 0.5f / kFps) return &k;
        return nullptr;
    }
    void setKey(float t, float v) {
        for (Keyframe& k : keys)
            if (std::abs(k.t - t) < 0.5f / kFps) {
                k.v = v;
                return;
            }
        auto at = std::upper_bound(keys.begin(), keys.end(), t, [](float x, const Keyframe& k) { return x < k.t; });
        keys.insert(at, {t, v});
    }
    std::optional<float> eraseKey(float t) {
        const Keyframe* k = keyAt(t);
        if (!k) return std::nullopt;
        const float v = k->v;
        keys.erase(keys.begin() + (k - keys.data()));
        return v;
    }
    // User deletion: removing the last key keeps its value, so the parameter does not jump.
    void removeKey(float t) {
        if (auto v = eraseKey(t); v && keys.empty()) value = *v;
    }
};

class NodeView; // the editor's widget for a node (main.cpp)

struct Node {
    NodeType type;
    std::string name;
    SkPoint pos;                   // top-left in node-editor content coordinates
    std::vector<Param> params;     // one per typeDef(type).params
    std::array<Node*, 4> inputs{}; // wired source per input; nullptr = default value
    bool selected = false;
    NodeView* view = nullptr;

    float at(int p, float t) const { return params[p].at(t, !typeDef(type).params[p].choices.empty()); }
    // Every time that holds a key in any parameter, ascending, one per frame.
    std::vector<float> keyTimes() const {
        std::vector<float> ts;
        for (const Param& p : params)
            for (const Keyframe& k : p.keys) ts.push_back(k.t);
        std::sort(ts.begin(), ts.end());
        ts.erase(std::unique(ts.begin(), ts.end(), [](float a, float b) { return b - a < 0.5f / kFps; }), ts.end());
        return ts;
    }
};

using KeyRef = std::pair<Node*, float>; // all keys of a node at one time (a diamond on its track)

// Moves the keys at each (node, time) by dt. Processed so that keys moving together never overwrite
// each other; a key that lands on an unselected one replaces it. Returns the new references.
inline std::vector<KeyRef> moveKeys(std::vector<KeyRef> sel, float dt) {
    std::sort(sel.begin(), sel.end(), [dt](const KeyRef& a, const KeyRef& b) { return dt > 0 ? a.second > b.second : a.second < b.second; });
    for (auto& [n, t] : sel) {
        const float to = snapFrame(t + dt);
        for (Param& p : n->params)
            if (auto v = p.eraseKey(t)) p.setKey(to, *v);
        t = to;
    }
    return sel;
}

struct Clip {
    struct Item {
        NodeType type;
        SkPoint pos;
        std::vector<Param> params;
        std::array<int, 4> inputs; // index into items, -1 = not wired (links leaving the copy are dropped)
    };
    std::vector<Item> items;
    SkPoint topLeft() const {
        SkPoint p{1e9f, 1e9f};
        for (const Item& it : items) p = {std::min(p.x(), it.pos.x()), std::min(p.y(), it.pos.y())};
        return p;
    }
};

struct Doc {
    std::vector<std::unique_ptr<Node>> nodes; // list / timeline order
    int counters[kNodeTypes] = {};

    Node* add(NodeType t, SkPoint pos) {
        auto n = std::make_unique<Node>();
        n->type = t;
        n->name = std::string(typeDef(t).name) + " " + std::to_string(++counters[int(t)]);
        n->pos = pos;
        for (const ParamDef& d : typeDef(t).params) n->params.push_back({d.def, {}});
        nodes.push_back(std::move(n));
        return nodes.back().get();
    }
    // False (and no change) if the output does not match the input's type.
    bool connect(Node* from, Node* to, int input) {
        const TypeDef& d = typeDef(to->type);
        if (!typeDef(from->type).hasOutput || input >= int(d.inputs.size()) || Port(int(from->type)) != d.inputs[input])
            return false;
        to->inputs[input] = from;
        return true;
    }
    void selectOnly(Node* n) {
        for (auto& m : nodes) m->selected = m.get() == n;
    }
    std::vector<Node*> selection() const {
        std::vector<Node*> s;
        for (auto& n : nodes)
            if (n->selected) s.push_back(n.get());
        return s;
    }
    // Callers drop their references (views, key selections) to the selected nodes first.
    void removeSelected() {
        for (auto& n : nodes)
            for (Node*& in : n->inputs)
                if (in && in->selected) in = nullptr;
        std::erase_if(nodes, [](const auto& n) { return n->selected; });
    }
    Clip copySelected() const {
        const std::vector<Node*> sel = selection();
        Clip c;
        for (const Node* n : sel) {
            Clip::Item it{n->type, n->pos, n->params, {-1, -1, -1, -1}};
            for (int i = 0; i < 4; ++i) {
                auto f = std::find(sel.begin(), sel.end(), n->inputs[i]);
                if (n->inputs[i] && f != sel.end()) it.inputs[i] = int(f - sel.begin());
            }
            c.items.push_back(std::move(it));
        }
        return c;
    }
    // Adds the clip's nodes moved by `offset`, wired among themselves; they become the selection.
    std::vector<Node*> paste(const Clip& c, SkVector offset) {
        selectOnly(nullptr);
        std::vector<Node*> made;
        for (const Clip::Item& it : c.items) {
            Node* n = add(it.type, it.pos + offset);
            n->params = it.params;
            n->selected = true;
            made.push_back(n);
        }
        for (size_t i = 0; i < made.size(); ++i)
            for (int k = 0; k < 4; ++k)
                if (c.items[i].inputs[k] >= 0) made[i]->inputs[k] = made[c.items[i].inputs[k]];
        return made;
    }
};

// One shape in the preview: an Output node with its inputs evaluated at a time.
// Unwired inputs fall back to these defaults.
struct Drawn {
    int kind = 0, blend = 0;
    float x = kSceneW / 2, y = kSceneH / 2, w = 80, h = 80, rotation = 0, stroke = 0;
    float hue = 0, sat = 0, val = 1, alpha = 1, opacity = 1;
};

inline Drawn evaluate(const Node& out, float t) {
    Drawn d;
    d.blend = int(out.at(kBlend, t));
    d.opacity = out.at(kOpacity, t);
    if (const Node* n = out.inputs[kInShape]) {
        d.kind = int(n->at(kKind, t));
        d.rotation = n->at(kRotation, t);
        d.stroke = n->at(kStroke, t);
    }
    if (const Node* n = out.inputs[kInPosition]) d.x = n->at(kX, t), d.y = n->at(kY, t);
    if (const Node* n = out.inputs[kInSize]) d.w = n->at(kWidth, t), d.h = n->at(kHeight, t);
    if (const Node* n = out.inputs[kInColor]) {
        d.hue = n->at(kHue, t);
        d.sat = n->at(kSat, t);
        d.val = n->at(kVal, t);
        d.alpha = n->at(kAlpha, t);
    }
    return d;
}

// The startup scene: `groups` x (Shape, Position, Size, Color -> Output) = 5 nodes each, laid out in a
// grid in the editor, drawing a grid of shapes in the preview. Deterministic; about half the
// parameters carry keyframes.
inline void buildDemo(Doc& doc, int groups) {
    std::mt19937 rng(7);
    auto rnd = [&](float a, float b) { return std::uniform_real_distribution<float>(a, b)(rng); };
    constexpr int kCols = 20;
    for (int g = 0; g < groups; ++g) {
        const int gx = g % kCols, gy = g / kCols;
        const float ox = gx * 500.0f, oy = gy * 620.0f;
        Node* shape = doc.add(NodeType::Shape, {ox, oy});
        Node* pos = doc.add(NodeType::Position, {ox, oy + 150});
        Node* size = doc.add(NodeType::Size, {ox, oy + 270});
        Node* color = doc.add(NodeType::Color, {ox, oy + 390});
        Node* out = doc.add(NodeType::Output, {ox + 250, oy + 190});
        out->inputs = {shape, pos, size, color};

        const float cx = 60 + gx * 95.0f, cy = 40 + (gy % 20) * 52.0f, s = rnd(22, 40), hue = gx * 18.0f;
        pos->params[kX].value = cx;
        pos->params[kY].value = cy;
        size->params[kWidth].value = size->params[kHeight].value = s;
        shape->params[kKind].value = float(g % 5);
        shape->params[kStroke].value = g % 7 == 0 ? 3.0f : 0.0f;
        color->params[kHue].value = hue;
        color->params[kSat].value = 0.7f;
        color->params[kVal].value = 0.95f;

        if (g % 2 == 0) { // orbit, a key every 5 s
            const float r = rnd(12, 36), phase = rnd(0, 6.28f);
            for (int k = 0; k <= 6; ++k) {
                const float a = phase + k * 1.5708f;
                pos->params[kX].setKey(k * 5.0f, cx + std::cos(a) * r);
                pos->params[kY].setKey(k * 5.0f, cy + std::sin(a) * r);
            }
        }
        if (g % 3 == 0) {
            color->params[kHue].setKey(0, hue);
            color->params[kHue].setKey(15, 360 - hue);
            color->params[kHue].setKey(30, hue);
        }
        if (g % 4 == 1) {
            shape->params[kRotation].setKey(0, 0);
            shape->params[kRotation].setKey(30, g % 8 == 1 ? 360.0f : -360.0f);
        }
        if (g % 5 == 2) // pulse
            for (int k = 0; k <= 12; ++k) {
                size->params[kWidth].setKey(k * 2.5f, k % 2 ? s * 1.6f : s);
                size->params[kHeight].setKey(k * 2.5f, k % 2 ? s * 1.6f : s);
            }
        if (g % 10 == 3)
            for (int k = 0; k < 3; ++k) shape->params[kKind].setKey(k * 10.0f, float((g + k) % 5));
        if (g % 6 == 5) {
            color->params[kAlpha].setKey(0, 1);
            color->params[kAlpha].setKey(rnd(8, 22), 0.15f);
            color->params[kAlpha].setKey(30, 1);
        }
    }
}

} // namespace NodeEd
