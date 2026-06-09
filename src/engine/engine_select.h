// SYNTHUX ACADEMY /////////////////////////////////////////
// SPOTYKACH ///////////////////////////////////////////////
#pragma once

// Build-time engine selection (item 3b-1). The Makefile's ENGINE variable emits one
// -DSPK_ENGINE_* define; this header maps it to the concrete `ActiveEngine` type that app.cpp
// instantiates as a static value member. The platform (CoreUI/Storage) only ever sees IEngine,
// so this is the single place the firmware names a concrete engine. See docs/architecture.md.

#if defined(SPK_ENGINE_GRANULAR)
  #include "engine/granular/granular_engine.h"
  namespace spotykach { using ActiveEngine = GranularEngine; }
#elif defined(SPK_ENGINE_PASSTHROUGH)
  #include "engine/passthrough/passthrough_engine.h"
  namespace spotykach { using ActiveEngine = PassthroughEngine; }
#elif defined(SPK_ENGINE_DELAY)
  #include "engine/delay/delay_engine.h"
  namespace spotykach { using ActiveEngine = DelayEngine; }
#elif defined(SPK_ENGINE_EDRUMS)
  #include "engine/edrums/edrums_engine.h"
  namespace spotykach { using ActiveEngine = EdrumsEngine; }
#elif defined(SPK_ENGINE_RESO)
  #include "engine/reso/reso_engine.h"
  namespace spotykach { using ActiveEngine = ResoEngine; }
#elif defined(SPK_ENGINE_TAPE)
  #include "engine/tape/tape_engine.h"
  namespace spotykach { using ActiveEngine = TapeEngine; }
#elif defined(SPK_ENGINE_FAUST)
  #include "engine/faust/faust_engine.h"
  namespace spotykach { using ActiveEngine = FaustEngine; }
#else
  #error "No engine selected: build with ENGINE=granular (default), passthrough, delay, edrums, reso, tape, or faust"
#endif
