// SYNTHUX ACADEMY /////////////////////////////////////////
// SPOTYKACH ///////////////////////////////////////////////
#pragma once

#include <array>
#include <random>

#include "nocopy.h"
#include "config.h"
#include "engine/engine_context.h"

#include "mode.h"     // Mode + Route (Route moved here from core.h)
#include "driver.h"
#include "modulator.h"
#include "xfade.h"
#include "panner.h"
#include "dsp/smooth.h"

namespace spotykach {

// ModType now lives in the contract (engine/mode.h, via the mode.h include above); item 5b.
// It previously had a duplicate definition here - removing it unifies the two.

class Core {
public:
  Core();
  ~Core() {}
  
  void init(const EngineContext& ctx);

  Driver& driver() { return _driver; }
  Panner& panner() { return _panner; }
  Deck& deck(const DeckRef::Ref ref) { return _decks[ref]; }
  Modulator& mod(const DeckRef::Ref ref) { return _mod[ref]; }

  Deck::Source source(const DeckRef::Ref deck) const { return _source[deck]; }
  void set_source(const Deck::Source source, const DeckRef::Ref deck) { _source[deck] = source; }
  void infer_panner_mode();

  float mix() const { return _xfade.Stage(); }
  void set_mix(const float value) { _mix = value; }
  void mix_mod_in(const float value) { 
    if (value < 0.01) _mix_mod = 0.f;
    else _mix_mod = value; 
  }

  void set_route(const Route val);
  Route route() const { return _route; }

  void set_click_mix(const float value) { _click_mix = value; }
  
  void prepare();
  void process(const float* const* in, float** out, size_t size);

private:
  NOCOPY(Core)
  
  void _resolve_mix();

  static constexpr uint8_t kNotesCount = 7;
  static constexpr uint8_t kVoxCount = 4;

  std::array<Deck, DeckRef::Count> _decks;
  std::array<Modulator, DeckRef::Count> _mod;
  // Per-deck float*[2] holders for the detector/delay buffers sub-allocated from the engine arena
  // (Deck::Params wants float**); populated in init(). Item: EngineBuffers generalization (Stage 2).
  float* _detect_buf[DeckRef::Count][2];
  float* _delay_buf[DeckRef::Count][2];
  Driver  _driver;
  XFade   _xfade;
  Click   _click;
  Panner  _panner;
  OnePoleSmoother _mix_smooth;

  std::array<float, DeckRef::Count> _pos_mod_amnt;
  std::array<Deck::Source, DeckRef::Count> _source;

  Route _route;

  std::array<float, 2> _reverb_in;
  std::array<float, 2> _reverb_out;
  std::array<float, 2> _bus;

  float _click_mix;
  float _mix;
  float _mix_mod;
};

};
