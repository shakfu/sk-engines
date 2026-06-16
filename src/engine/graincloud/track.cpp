#include "track.h"
#include <cstring>

using namespace spotykach;

Track::Track():
_buffer          { nullptr },
_current_event   { nullptr },
_tick_counter    { 0 },
_write_slot      { 0 },
_read_slot       { 0 },
_slice_slot      { 0 },
_last_hit_slot   { kNone },
_rec_counter     { 0 },
_rec_start       { kNone },
_rec_end         { kNone },
_is_armed        { false },
_is_recording    { false },
_is_clearing     { false },
_is_empty        { true }
{};

void Track::init(Event* buf) {
    _buffer = buf;
    clear();
};

void Track::tick(const bool is_key_tick) {
    if (_is_auto_cutting && is_key_tick) {
        if (_is_armed && !_is_recording) _start_recording();
        else if (_is_recording && !_is_armed) _stop_recording();
    }

    // Remember current slot
    auto slot = _slice_slot;
    // The slot is advanced every two ticks,
    // and one tick before actual onset position.
    if (++_tick_counter >= 2) {
        _tick_counter = 0;
        // Advance and wrap the current slot.
        if (++_slice_slot >= _slice.size()) {
            if (!_is_recording || _rec_end != kNone) { 
                _make_slice(); 
            }
            _slice_slot = 0;
        }
        if (_is_recording) {
            _advance_write_slot();
            _rec_counter++;
        }
        // Clear slot if in clearing mode.
        if (_is_clearing) _clear(_write_slot);
    }

    // If the slot was not advanced
    // it means we're at the onset possion,
    // and if this position is not empty
    // in the slice array - the drum should be triggered.
    // A check against _last_hit_slot is
    // to prevent double triggering during recording.Whts
    if (slot == _slice_slot && _slice[_slice_slot] != nullptr && _slice[_slice_slot]->on && _slice_slot != _last_hit_slot) {
        _current_event = _slice[_slice_slot];
    }
    else {
        _current_event = nullptr;
    }
    // Reset last hit slot.
    _last_hit_slot = kNone;
};

void Track::arm(const bool auto_start) 
{
    if (_is_empty) rewind();
    _is_armed = true;
    _is_auto_cutting = auto_start;
};

void Track::disarm(const bool force)
{
    _is_armed = false;
    if (force || !_is_auto_cutting) _stop_recording();
}

void Track::_start_recording()
{
    _write_slot = _read_slot;
    if (_rec_start == kNone) {
        _rec_start = _write_slot;
    }
    _is_recording = true;
    rewind();
}

void Track::_stop_recording()
{
    _is_recording = false;
    _is_auto_cutting = false;
    if (_rec_end == kNone) {
        _rec_end = _write_slot;
        if (_rec_counter > kLength) _rec_start = (_write_slot > 0) ? _write_slot - 1 : 0;
    }
    rewind();
}

void Track::add_event(const Event *event) {
    if (!_is_auto_cutting && _is_armed && !_is_recording) _start_recording();
    if (!_is_recording) return;

    auto e = _buffer + _write_slot;
    e->on = event->on;
    e->p1_on = event->p1_on;
    e->p1 = event->p1;
    e->p2_on = event->p2_on;
    e->p2 = event->p2;
    e->p3_on = event->p3_on;
    e->p3 = event->p3;
    e->p4_on = event->p4_on;
    e->p4 = event->p4;
    e->discont = true;
    _last_hit_slot = _slice_slot;
    _is_empty = false;
};

void Track::add_p1(const float value) {
    if (!_is_recording) return;
    auto e = _buffer + _write_slot;
    e->p1 = value;
    e->p1_on = true;
}

void Track::add_p2(const float value) {
    if (!_is_recording) return;
    auto e = _buffer + _write_slot;
    e->p2 = value;
    e->p2_on = true;
}

void Track::add_p3(const float value) {
    if (!_is_recording) return;
    auto e = _buffer + _write_slot;
    e->p3 = value;
    e->p3_on = true;
}

void Track::add_p4(const float value) {
    if (!_is_recording) return;
    auto e = _buffer + _write_slot;
    e->p4 = value;
    e->p4_on = true;
}

void Track::rewind() 
{
    _slice_slot = 0;
    _tick_counter = 0;
    _write_slot = _rec_start == kNone ? 0 : _rec_start;
    _read_slot = _rec_start == kNone ? 0 : _rec_start;
    _rec_counter = 0;
    if (!_is_recording) _make_slice();
};

void Track::clear() {
    _is_empty = true;
    _rec_start = kNone;
    _rec_end = kNone;
    _current_event = nullptr;
    _slice.fill(nullptr);
    std::memset(_buffer, 0, sizeof(Event) * kLength);
    rewind();
}

void Track::_make_slice() {
    for (uint16_t i = 0; i < kSliceLength; i++) {
        _slice[i] = _buffer + _read_slot;
        _advance_read_slot();
    }
};

void Track::_advance_read_slot() {
    _read_slot ++;
    if (_read_slot >= kLength) { 
        _read_slot = 0; 
    }
    else if (_read_slot == _rec_end) { 
        _read_slot = _rec_start;  
    }
};

void Track::_advance_write_slot() {
    if (_write_slot >= kLength) { _write_slot = 0; }
    else if (_write_slot == _rec_end) { _write_slot = _rec_start; }
    else _write_slot ++;
}
