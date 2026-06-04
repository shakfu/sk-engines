#pragma once

#include <daisy_seed.h>
#include "nocopy.h"

class Meter {
public:
    static Meter& cpu() {
        static Meter instance;
        return instance;
    }

    daisy::CpuLoadMeter load;

private:
    Meter() = default;
    NOCOPY(Meter)
};
