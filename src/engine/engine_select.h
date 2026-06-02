// SYNTHUX ACADEMY /////////////////////////////////////////
// SPOTYKACH ///////////////////////////////////////////////
#pragma once

// Build-time engine selection (item 3b-1). The Makefile's ENGINE variable emits one
// -DSPK_ENGINE_* define; this header maps it to the concrete `ActiveEngine` type that app.cpp
// instantiates as a static value member. The platform (CoreUI/Storage) only ever sees IEngine,
// so this is the single place the firmware names a concrete engine. See docs/architecture.md.

#if defined(SPK_ENGINE_GRANULAR)
  #include "engine/granular_engine.h"
  namespace spotykach { using ActiveEngine = GranularEngine; }
#elif defined(SPK_ENGINE_PASSTHROUGH)
  #include "engine/passthrough_engine.h"
  namespace spotykach { using ActiveEngine = PassthroughEngine; }
#else
  #error "No engine selected: build with ENGINE=granular (default) or ENGINE=passthrough"
#endif
