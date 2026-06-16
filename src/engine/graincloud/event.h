#pragma once

#include <cstring>

namespace spotykach {

    struct Event {
        float p1;
        float p2;
        float p3;
        float p4;

        bool p1_on;
        bool p2_on;
        bool p3_on;
        bool p4_on;

        bool on;
        
        /* Discontinuous events are those not trailed with V/Oct change. 
        Tese are MIDI events and events from the track. */
        bool discont; 
    };

    inline Event make_event() {
        Event e;
        std::memset(&e, 0, sizeof(Event));
        e.on = true;

        return e;
    };
};
