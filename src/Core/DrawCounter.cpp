// Compiled without RTTI (see CMakeLists.txt): Skia is built with -fno-rtti, so a class deriving from
// one of its canvases cannot have typeinfo either.
#include <memory>
#include "include/utils/SkPaintFilterCanvas.h"

namespace Terracotta2 {

namespace {
// Forwards everything to the target canvas, counting draws (FrameStats::drawCalls).
class CountingCanvas : public SkPaintFilterCanvas {
public:
    CountingCanvas(SkCanvas* target, int* count) : SkPaintFilterCanvas(target), m_target(target), m_count(count) {}
    skgpu::graphite::Recorder* recorder() const override { return m_target->recorder(); } // for GPU image uploads
    SkRecorder* baseRecorder() const override { return m_target->baseRecorder(); }

protected:
    bool onFilter(SkPaint&) const override {
        ++*m_count;
        return true;
    }

private:
    SkCanvas* m_target;
    int* m_count;
};
} // namespace

std::unique_ptr<SkCanvas> makeCountingCanvas(SkCanvas* target, int* count) { return std::make_unique<CountingCanvas>(target, count); }

} // namespace Terracotta2
