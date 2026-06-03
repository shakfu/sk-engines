#pragma once

// Mode + Route moved to the contract (engine/mode.h) in item 5b so the IEngine surface no longer
// pulls the granular DSP. This redirect keeps the granular DSP's relative `#include "mode.h"` working.
#include "engine/mode.h"
