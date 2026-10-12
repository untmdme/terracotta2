// Checks the node editor model (no window): keyframes, wiring, copy/paste, deletion, evaluation.
// Run: build/<preset>/app/Node_Editor_Test  (exits non-zero on failure)
#undef NDEBUG
#include <cassert>
#include <cstdio>
#include "model.hpp"

using namespace NodeEd;

int main() {
    // keyframes: hold, ease, stepped, replace within a frame, delete keeps the value
    Param p{5, {}};
    assert(p.at(3, false) == 5);
    p.setKey(10, 100);
    p.setKey(0, 0);
    p.setKey(10.01f, 200); // same frame: replaces
    assert(p.keys.size() == 2 && p.keys[0].t == 0 && p.keys[1].v == 200);
    assert(p.at(-1, false) == 0 && p.at(20, false) == 200);
    assert(p.at(5, false) == 100);   // smoothstep midpoint
    assert(p.at(2, false) < 2 * 20); // eased in: slower than linear at the start
    assert(p.at(9, true) == 0);      // stepped holds the previous key
    p.removeKey(0);
    p.removeKey(10);
    assert(!p.animated() && p.value == 200);

    // wiring: types must match
    Doc doc;
    Node* shape = doc.add(NodeType::Shape, {0, 0});
    Node* pos = doc.add(NodeType::Position, {0, 100});
    Node* out = doc.add(NodeType::Output, {200, 0});
    assert(!doc.connect(pos, out, kInShape));
    assert(doc.connect(shape, out, kInShape) && doc.connect(pos, out, kInPosition));
    assert(!doc.connect(out, out, kInShape)); // outputs have no output port

    // evaluation: wired values, unwired defaults
    pos->params[kX].setKey(0, 100);
    pos->params[kX].setKey(10, 300);
    Drawn d = evaluate(*out, 10);
    assert(d.x == 300 && d.y == kSceneH / 2 && d.w == 80);

    // key times, and moving keys onto each other's frames without loss
    pos->params[kY].setKey(5, 1);
    assert(pos->keyTimes().size() == 3);
    auto moved = moveKeys({{pos, 0.0f}, {pos, 5.0f}}, 5);
    assert(moved.size() == 2 && pos->params[kX].keyAt(5) && pos->params[kX].keyAt(5)->v == 100);
    assert(pos->params[kY].keyAt(10) && pos->params[kY].keyAt(10)->v == 1);
    assert(pos->params[kX].keys.size() == 2 && pos->params[kX].keyAt(10)->v == 300); // unselected key untouched

    // copy / paste: links inside the copy are kept, links leaving it are dropped
    doc.selectOnly(out);
    shape->selected = true;
    Clip clip = doc.copySelected();
    auto made = doc.paste(clip, {50, 50});
    assert(made.size() == 2 && doc.nodes.size() == 5);
    Node* newOut = made[0]->type == NodeType::Output ? made[0] : made[1];
    Node* newShape = made[0] == newOut ? made[1] : made[0];
    assert(newOut->inputs[kInShape] == newShape && newOut->inputs[kInPosition] == nullptr);
    assert(newOut->pos.x() == 250 && newOut->selected && !out->selected);
    assert(newShape->name == "Shape 2");

    // delete clears dangling inputs
    doc.selectOnly(pos);
    doc.removeSelected();
    assert(doc.nodes.size() == 4 && out->inputs[kInPosition] == nullptr);

    // demo scene size
    Doc demo;
    buildDemo(demo, 400);
    assert(demo.nodes.size() == 2000);

    std::printf("model_test: ok\n");
}
