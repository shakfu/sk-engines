
#include <string.h>
#include "../hw/buffer.sdram.h"
#include "storage.h"
#include "engine/iengine.h"
#include "wav.h"

using namespace spotykach;

static const std::string kRootDir = "SK";
static const std::string A = "A";
static const std::string B = "B";
static const std::string kTapeName[kStorageTapeCount] = {
    "B", // B lue
    "G", // G reen
    "P", // P ink
    "R", // R ed
    "T", // T urquose
    "Y"  // Y ellow
};
static const std::string kSlotName[kStorageSlotCount] = { "1", "2", "3", "4", "5", "6" };

// "<slot>.WAV", e.g. "1.WAV" (needs 6 bytes incl. null). Manual join avoids
// pulling the newlib printf/dtoa machinery in for a plain string concatenation.
inline void audio_file_name(const uint8_t slot_idx, char* out_name)
{
    strcpy(out_name, kSlotName[slot_idx].c_str());
    strcat(out_name, ".WAV");
}

// "/<root>/<tape>/<slot>.WAV", e.g. "/SK/G/1.WAV" (needs 12 bytes incl. null).
inline void audio_file_path(const Deck::Ref deck, const uint8_t tape_idx, const uint8_t slot_idx, char* out_path)
{
    out_path[0] = '/';
    strcpy(out_path + 1, kRootDir.c_str());
    strcat(out_path, "/");
    strcat(out_path, kTapeName[tape_idx].c_str());
    strcat(out_path, "/");
    strcat(out_path, kSlotName[slot_idx].c_str());
    strcat(out_path, ".WAV");
}

DeckStorage::DeckStorage():
_tape_idx { kNone },
_slot_idx { kNone },
_recent_tape_idx { kNone },
_recent_slot_idx { kNone }
{}

void DeckStorage::init(Card* card, IEngine* engine, Deck::Ref ref)
{
    _card = card;
    _engine = engine;
    _ref = ref;
    _deck_dir = _ref == Deck::A ? A : B;
}

void DeckStorage::set_on_save_audio(std::function<void(const Deck::Ref)> on_saved)
{
    _on_audio_saved = on_saved;
}

void DeckStorage::activate()
{
    if (_state != State::idle) return;
    if (_tape_idx == kNone) _tape_idx = 0;
    _state = State::selecting;
    _read_slots();
}

void DeckStorage::deactivate()
{
    _state = State::idle;
    _is_preloading = false;
}

void DeckStorage::next_tape()
{
    _tape_idx ++;
    if (_tape_idx >= kStorageTapeCount) _tape_idx = 0;
    _read_slots();
}

void DeckStorage::previous_tape()
{
    if (_tape_idx > 0) _tape_idx --;
    else _tape_idx = kStorageTapeCount - 1;
    _read_slots();
}

void DeckStorage::_read_slots()
{
    if (_state != State::selecting) return;
    char audio_path[12]; // /A/G/1.WAV
    for (size_t i = 0; i < _slots.size(); i++) {
        audio_file_path(_ref, _tape_idx, i, audio_path);
        _slots[i].is_empty = !_card->file_exists(audio_path);
    }
    _slot_idx = _tape_idx == _recent_tape_idx ? _recent_slot_idx : kNone;
}

void DeckStorage::_fill_audio_data(Card::AudioData& ad, char* name) const
{
    ad.root_dir = (char *)kRootDir.c_str();
    ad.deck_dir = (char *)_deck_dir.c_str();
    ad.tape_dir = (char*)kTapeName[_tape_idx].c_str();

    audio_file_name(_slot_idx, name);
    ad.file_name = name;
}

void DeckStorage::save()
{
    if (_engine->audio_is_empty(_ref)) return;

    Card::AudioData ad;

    auto body_size = _engine->audio_recorded_bytes(_ref);
    ad.body = _engine->audio_data(_ref);
    ad.body_size = body_size;

    auto header = wav_header(body_size);
    ad.header = reinterpret_cast<uint8_t*>(&header);
    ad.header_size = sizeof(header);

    char name[6];
    _fill_audio_data(ad, name);

    _card->init_write_audio(ad);
    _state = State::saving;
    
    _recent_slot_idx = _slot_idx;
    _recent_tape_idx = _tape_idx;
}

void DeckStorage::load()
{
    if (!can_load()) return;

    _recent_tape_idx = _tape_idx;
    _recent_slot_idx = _slot_idx;

    Card::AudioData ad;

    ad.body = _engine->audio_data(_ref);
    ad.body_size = _engine->audio_capacity_bytes(_ref);

    char name[6];
    _fill_audio_data(ad, name);

    _card->init_read_audio(ad);
    _state = State::loading;
}

void DeckStorage::cancel()
{
    _card->cancel();
    _clear_preload_source();
    deactivate();
    _recent_slot_idx = kNone;
    _recent_tape_idx = kNone;
}

static constexpr size_t kMemSize = 4;
void DeckStorage::preload()
{
    uint8_t tape, slot;
    if (!_read_preload_source(tape, slot)) return;
    char audio_path[12]; // /SK/G/1.WAV
    audio_file_path(_ref, tape, slot, audio_path);
    if (_card->file_exists(audio_path)) {
        _tape_idx = tape;
        _slot_idx = slot;
        _is_preloading = true;
        load();
    }
}
bool DeckStorage::_read_preload_source(uint8_t& tape, uint8_t& slot)
{
    uint8_t* data = nullptr;
    size_t size = 0;
    if (!_card->read_file((char* )kMemory.c_str(), data, &size)) return false;
    /*
    Expecting four digits (1-6): [tape A, slot A, tape B, slot B].
    If tape/slot is 0, no preload happening.
    */
    if (size < kMemSize) return false;
    switch (_ref) {
        case Deck::A: {
            tape = data[0] - '0';
            slot = data[1] - '0';
            break;
        }
        case Deck::B: {
            tape = data[2] - '0';
            slot = data[3] - '0';
            break;
        }
        default: 
            return false;
    }
    if (tape == 0 || slot == 0) return false;
    tape -= 1;
    slot -= 1;
    return true;
}
static void write_preload_source(Card* card, const Deck::Ref deck, const uint8_t tape, const uint8_t slot)
{
    uint8_t* read_data = nullptr;
    size_t size;
    card->read_file((char*)kMemory.c_str(), read_data, &size);
    
    uint8_t data[kMemSize] = { '0', '0', '0', '0' };
    if (read_data != nullptr && size >= kMemSize) {
        memcpy(data, read_data, size);
    }
    auto tape_char = (tape + 1) + '0';
    auto slot_char = (slot + 1) + '0';
    switch (deck) {
        case Deck::A:
            data[0] = tape_char;
            data[1] = slot_char;
            break;

        case Deck::B:
            data[2] = tape_char;
            data[3] = slot_char;
            break;

        default: break;
    }
    card->write_file((char*)kMemory.c_str(), data, kMemSize);
}
void DeckStorage::_save_preload_source()
{
    write_preload_source(_card, _ref, _tape_idx, _slot_idx);
}
void DeckStorage::_clear_preload_source()
{
    write_preload_source(_card, _ref, kNone, kNone);
}

void DeckStorage::process() 
{
    static auto error_detected = false;
    if (!error_detected && _card->state() == Card::State::failed) {
        _state = State::error;
        error_detected = true;
    }

    switch (_state) {
        case State::idle: return;
        case State::selecting: return;
        case State::error:
            /* the error will be procesed on the second pass 
            so the ui has a chance to pick up and show error */
            if (!error_detected) deactivate();
            error_detected = false;
            break;

        case State::loading:
            if (_card->state() == Card::State::read_audio) _card->read_audio();
            else { 
                if (_card->notify_finish_processing()) {
                    _engine->audio_apply_loaded(_ref, _card->size_audio());
                    if (!_is_preloading && Config::dynamic().is_preload_on()) _save_preload_source();
                }
                _is_preloading = false;
                deactivate();
            }
            break;

        case State::saving:
            if (_card->state() == Card::State::write_audio) _card->write_audio();
            else {
                if (_card->notify_finish_processing()) {
                    _on_audio_saved(_ref);
                    if (Config::dynamic().is_preload_on()) _save_preload_source();
                }
                deactivate();
            }
            break;
    }
}
