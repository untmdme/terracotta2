#pragma once
#include <algorithm>
#include <functional>

namespace Terracotta2 {

enum class Ease { Smooth, Linear }; // Linear for loops (spinners), Smooth for transitions

// Time-based tween. Owned by a Widget; the Window ticks it while active.
class Animation {
    std::function<void(float)> m_onUpdate;
    std::function<void()> m_onComplete;
    float m_duration = 0.0f;
    float m_elapsed = 0.0f;
    bool m_active = false;
    Ease m_ease = Ease::Smooth;

public:
    void start(float durationSec, std::function<void(float)> onUpdate, std::function<void()> onComplete = nullptr,
               Ease ease = Ease::Smooth) {
        m_ease = ease;
        m_duration = durationSec;
        m_elapsed = 0.0f;
        m_onUpdate = std::move(onUpdate);
        m_onComplete = std::move(onComplete);
        m_active = true;
    }

    // Returns true while still running.
    bool update(float dt) {
        if (!m_active) return false;
        m_elapsed += dt;
        float t = m_duration > 0.0f ? std::clamp(m_elapsed / m_duration, 0.0f, 1.0f) : 1.0f;
        float eased = m_ease == Ease::Linear ? t : t * t * (3.0f - 2.0f * t); // smoothstep
        if (m_onUpdate) m_onUpdate(eased);
        if (t >= 1.0f) {
            m_active = false;
            auto done = std::move(m_onComplete); // onComplete may start() again (loops)
            m_onComplete = nullptr;
            if (done) done();
            return m_active;
        }
        return true;
    }

    void stop() { m_active = false; }
    bool isActive() const { return m_active; }
};

} // namespace Terracotta2
