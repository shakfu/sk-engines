#include "cpattern.h"
#include <stdint.h>

using namespace spotykach;

CPattern::CPattern():
_current_length { 0 },
_steps          { kSize },
_onsets         { 0 },
_max_onsets     { kSize },
_next_point     { 0 },
_shift          { 0 }
{
    set_onsets(7);
};

void CPattern::set_length(uint8_t steps) {
    steps = std::clamp<uint8_t>(steps, 1, kSize);
    if (steps == _steps) return;
    _steps = steps;
    if (_onsets > _steps) _onsets = _steps;
    if (_shift >= _steps) _shift = 0;
    if (_next_point >= _steps) _next_point = 0;
    _build();
}

void CPattern::plus_onset() {
    auto onsets = _onsets;
    if (onsets < _max_onsets) onsets ++;
    set_onsets(onsets);
}
void CPattern::minus_onset() {
    auto onsets = _onsets;
    if (onsets > 0) onsets--;
    set_onsets(onsets);
}

void CPattern::set_onsets(const uint8_t onsets) {
    const uint8_t clamped = std::min<uint8_t>(onsets, _steps);
    if (clamped == _onsets) return;
    _onsets = clamped;
    _build();
};

// Christoffel-word algorithm (from "Creating Rhythms", Hollos & Hollos), generalized from the fixed
// 16-slot version to distribute _onsets ones as evenly as possible over the active _steps slots.
void CPattern::_build() {
    const int n = _steps;
    _pattern.fill(0);
    _length.fill(0);

    if (_onsets == 0) return;

    if (_onsets >= n) {
        for (int i = 0; i < n; i++) _pattern[i] = 1;
    }
    else {
        int y = _onsets, a = y;
        int x = n - _onsets, b = x;
        int i = 0;
        _pattern[i++] = 1;
        while (a != b) {
            if (a > b) { _pattern[i] = 1; b += x; }
            else       { _pattern[i] = 0; a += y; }
            i++;
        }
        _pattern[i++] = 0;

        // Non-coprime case: only part of the slots got filled, so repeat the generated cell.
        if (i < n) {
            const int offset = i;
            int j = 0;
            while (j + offset < n) { _pattern[j + offset] = _pattern[j]; j++; }
        }
    }

    // Run-length to the next onset, used by length() (gap of the current onset).
    uint8_t count = 1;
    for (int i = n - 1; i >= 0; i--) {
        if (_pattern[i]) { _length[i] = count; count = 1; }
        else             { count++; }
    }
};

bool CPattern::trigger() {
    const int n = _steps;

    // Apply shift
    int point = static_cast<int>(_next_point) - static_cast<int>(_shift);
    while (point < 0) point += n;
    while (point >= n) point -= n;

    // Advance to the next pattern point (wrap at the active length)
    if (++_next_point >= n) _next_point = 0;

    // Send trigger if pattern point is 1
    _current_length = _length[point];
    return _pattern[point] != 0;
};
