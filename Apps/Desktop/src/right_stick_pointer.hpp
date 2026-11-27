#pragma once

#include <SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace desktop_input {

// Maps the TrimUI Smart Pro's right analog stick (raw SDL joystick axes 2/3)
// to a logical desktop pointer. Call handleAxisEvent() for each SDL event,
// then update() once per frame. The returned bool is true only when the
// integer pointer position changed.
class RightStickPointer {
public:
    explicit RightStickPointer(int width = 1280,
                               int height = 720,
                               int deadzone = 6200,
                               float maxSpeedPixelsPerSecond = 900.0f)
        : width_(std::max(1, width)),
          height_(std::max(1, height)),
          deadzone_(std::clamp(deadzone, 0, 32000)),
          maxSpeed_(std::max(1.0f, maxSpeedPixelsPerSecond)) {}

    void handleAxisEvent(const SDL_Event& event) {
        if (event.type != SDL_JOYAXISMOTION) return;

        // SDL's raw joystick event IDs on the TrimUI Smart Pro are
        // left-stick X/Y = 0/1 and right-stick X/Y = 2/3.
        if (event.jaxis.axis == 2) axisX_ = event.jaxis.value;
        else if (event.jaxis.axis == 3) axisY_ = event.jaxis.value;
    }

    bool update(std::uint64_t nowMs, int& x, int& y) {
        if (!clockStarted_) {
            lastUpdateMs_ = nowMs;
            clockStarted_ = true;
            return false;
        }

        const std::uint64_t elapsedMs = nowMs >= lastUpdateMs_ ? nowMs - lastUpdateMs_ : 0;
        lastUpdateMs_ = nowMs;
        const float dt = std::min(0.05f, static_cast<float>(elapsedMs) / 1000.0f);
        if (dt <= 0.0f) return false;

        const float nx = filteredAxis(axisX_);
        const float ny = filteredAxis(axisY_);
        if (nx == 0.0f) remainderX_ = 0.0f;
        if (ny == 0.0f) remainderY_ = 0.0f;

        // A gentle power curve gives fine control near the deadzone and a
        // faster sweep across the screen at full stick deflection.
        remainderX_ += curvedSpeed(nx) * dt;
        remainderY_ += curvedSpeed(ny) * dt;

        const int stepX = static_cast<int>(remainderX_);
        const int stepY = static_cast<int>(remainderY_);
        remainderX_ -= static_cast<float>(stepX);
        remainderY_ -= static_cast<float>(stepY);
        if (stepX == 0 && stepY == 0) return false;

        const int nextX = std::clamp(x + stepX, 0, width_ - 1);
        const int nextY = std::clamp(y + stepY, 0, height_ - 1);
        if (nextX == x) remainderX_ = 0.0f;
        if (nextY == y) remainderY_ = 0.0f;

        const bool moved = nextX != x || nextY != y;
        x = nextX;
        y = nextY;
        return moved;
    }

private:
    float filteredAxis(int value) const {
        const int magnitude = std::min(32767, std::abs(value));
        if (magnitude <= deadzone_) return 0.0f;

        const float range = static_cast<float>(32767 - deadzone_);
        const float strength = range > 0.0f
            ? static_cast<float>(magnitude - deadzone_) / range
            : 1.0f;
        return (value < 0 ? -1.0f : 1.0f) * std::clamp(strength, 0.0f, 1.0f);
    }

    float curvedSpeed(float axis) const {
        const float magnitude = std::abs(axis);
        const float eased = magnitude * magnitude * (0.35f + 0.65f * magnitude);
        return (axis < 0.0f ? -1.0f : 1.0f) * eased * maxSpeed_;
    }

    int width_;
    int height_;
    int deadzone_;
    float maxSpeed_;
    int axisX_ = 0;
    int axisY_ = 0;
    float remainderX_ = 0.0f;
    float remainderY_ = 0.0f;
    std::uint64_t lastUpdateMs_ = 0;
    bool clockStarted_ = false;
};

} // namespace desktop_input
