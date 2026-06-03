#pragma once

#include <array>
#include <cstdint>
#include "color.h"

namespace spotykach
{
    // A hardware-free ring drawing canvas: all methods operate on the internal per-pixel
    // _colors/_brights buffers; nothing here knows about the LED hardware. The platform blits
    // it via the templated apply() below, supplying a pixel sink. This is the Option A foundation
    // for the LED migration - an engine fills a LEDRing in render(DisplayModel&) and the platform
    // realizes it. apply() keeps the double-buffer cache + is_updated handshake (main loop
    // produces, the TIM5 ISR consumes) inside the canvas where that state belongs.
    class LEDRing
    {
    public:
        LEDRing();
        ~LEDRing() = default;

        void set_hex_color(const uint32_t);
        void set_brightness(const float);
        void fill_brightness(const float brightness);

        void set_segment(float norm_start, float norm_size, const bool sharp = false);

        void set_point_hex_color(const uint32_t);

        void set_point(uint8_t idx, const float brightness);
        void add_point(float norm_position, const float brightness, const bool sharp = false, const bool over = true);

        bool is_updated() const { return _is_updated; }
        void set_updated() { _is_updated = true; }

        // Blit through a caller-supplied sink: void(uint8_t idx, uint32_t hex, float brightness).
        // The platform's sink maps idx -> the physical LED chain. Same cache/reset semantics as
        // the former apply(Hardware&, LedId) - behavior-identical, just hardware-free.
        template <class Sink>
        void apply(Sink set_pixel)
        {
            for (uint8_t i = 0; i < _brights.size(); i++) {
                if (_is_updated) {
                    _colors_cache[i] = _colors[i];
                    _brights_cache[i] = _brights[i];
                }
                set_pixel(i, _colors_cache[i].Hex(), _brights_cache[i]);
            }
            _is_updated = false;
        }

        void clear();

    private:
        void _set(const int8_t idx, const infrasonic::Color color, const float brightness, const bool overlay = false);

        static constexpr float kBrightnessMult = .8f;
        static constexpr int8_t kCount = 32;
        static constexpr int8_t kUpperBound = kCount - 1;

        std::array<infrasonic::Color, kCount> _colors;
        std::array<float, kCount> _brights;

        std::array<infrasonic::Color, kCount> _colors_cache;
        std::array<float, kCount> _brights_cache;

        infrasonic::Color _segment_color;
        infrasonic::Color _point_color;
        float _segment_brightness;

        bool _is_updated;
    };
}
