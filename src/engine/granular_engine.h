// SYNTHUX ACADEMY /////////////////////////////////////////
// SPOTYKACH ///////////////////////////////////////////////
#pragma once

#include "engine/iengine.h"
#include "core/core.h"
#include "nocopy.h"

namespace spotykach {

// The granular looper as an IEngine. Phase 2: it owns the Core graph and forwards the audio
// lifecycle to it. The UI/storage/CV paths still reach the graph through core() until the
// interaction layer is migrated into this class (Phase 3). The escape hatch is intentional
// and temporary - it is the seam those later increments collapse.
class GranularEngine : public IEngine {
public:
    GranularEngine() = default;
    ~GranularEngine() override = default;

    void init(const EngineContext& ctx) override { _core.init(ctx); }
    void prepare() override { _core.prepare(); }
    void process(const float* const* in, float** out, size_t size) override {
        _core.process(in, out, size);
    }

    // Temporary direct access for the still-coupled UI/storage/CV paths.
    Core& core() { return _core; }

private:
    NOCOPY(GranularEngine)

    Core _core;
};

};
