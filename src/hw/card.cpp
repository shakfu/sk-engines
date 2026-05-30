#include "card.h"
#include "../memory/wav.h"
#include <string.h>

using namespace spotykach;
using namespace daisy;

Card::Card():
_state { State::unmounted }
{}

void Card::init(uint8_t* buffer) {
    _buffer = buffer;
}

bool Card::file_exists(const char* path)
{
    FILINFO fno;
    return f_stat(path, &fno) == FR_OK && fno.fsize > 0; 
}

void Card::init_read_audio(const AudioData data) 
{   
    if (_state != State::idle) return;

    _size_read_audio = 0;

    char audio_path[12]; // "/SK/G/1.WAV"
    audio_path[0] = '/';
    strcpy(audio_path + 1, data.root_dir);
    strcat(audio_path, "/");
    strcat(audio_path, data.tape_dir);
    strcat(audio_path, "/");
    strcat(audio_path, data.file_name);

    WavHeader hdr;
    size_t hdr_size = 0;
    size_t bytesread = 0;
    
    if (f_open(&_sdfile, audio_path, FA_OPEN_EXISTING | FA_READ) != FR_OK) {
        _state = State::failed;
        return;
    }
    
    if (f_read(&_sdfile, _buffer, kChunk, &bytesread) != FR_OK
    || !wav_header(_buffer, kChunk, hdr, hdr_size)) {
        _state = State::failed;
        _close_file();
        return;
    }
    
    #pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
    // Accept both supported depths regardless of build: 32-bit float (AudioFormat 3) and
    // 16-bit PCM (AudioFormat 1). If the file's depth differs from this build's buffer
    // storage (kWav*), samples are converted on the fly in read_audio() - so float tapes
    // load into a 16-bit firmware and vice versa.
    auto fmt_ok = (hdr.AudioFormat == 3 && hdr.BitsPerSample == 32)
               || (hdr.AudioFormat == 1 && hdr.BitsPerSample == 16);
    if (hdr.NbrChannels == 2
        && hdr.SampleRate == 48000
        && fmt_ok
        && hdr.DataSize > 0) {
            _loader.begin((size_t)hdr.DataSize, hdr.BitsPerSample / 8,
                          data.body, data.body_size, kWavBytesPerSample);
            _offset = 0;
            _size   = _loader.size_bytes();  // keep progress() in sync
            _state  = State::read_audio;
    }
    else {
        _state = State::failed;
        _close_file();
    }

    if (f_lseek(&_sdfile, hdr_size) != FR_OK) {
        _state = State::failed;
        _close_file();
    }
}
void Card::read_audio()
{
    if (_state != State::read_audio) {
        return;
    }
    
    size_t bytesread;
    if (f_read(&_sdfile, _buffer, kChunk, &bytesread) != FR_OK) {
        _state = State::failed;
        _close_file();
        return;
    }

    // kChunk is a multiple of both sample widths and WAV data is sample-aligned, so every
    // chunk holds whole samples - no cross-chunk straddle. The loader converts (when the
    // file depth differs from the buffer) and tracks the byte/frame accounting.
    bool buffer_full = _loader.feed(_buffer, bytesread);
    _offset = _loader.offset();                  // keep progress() in sync
    _size_read_audio = _loader.frames();

    if (bytesread < kChunk || buffer_full) {
        _notify_finish_processing = true;
        _state = State::idle;
        _close_file();
        return;
    }
}

void Card::init_write_audio(const AudioData data)
{
    if (_state != State::idle) return;

    auto res = f_mkdir(data.root_dir);
    if (res != FR_OK && res != FR_EXIST) {
        _state = State::failed;
        return;
    }

    char tape_dir_path[5]; // "SK/G"
    strcpy(tape_dir_path, data.root_dir);
    strcat(tape_dir_path, "/");
    strcat(tape_dir_path, data.tape_dir);
    res = f_mkdir(tape_dir_path);
    if (res != FR_OK && res != FR_EXIST) {
        _state = State::failed;
        return;
    }

    char audio_path[11]; // "SK/G/1.WAV"
    strcpy(audio_path, tape_dir_path);
    strcat(audio_path, "/");
    strcat(audio_path, data.file_name);
    if (f_open(&_sdfile, audio_path, (FA_CREATE_ALWAYS) | (FA_WRITE)) != FR_OK) {
        _state = State::failed;
        return;
    }

    uint32_t byteswritten;
    if (f_write(&_sdfile, data.header, data.header_size, (UINT*)&byteswritten) == FR_OK) {
        _bytes = (uint8_t*)data.body;
        _size = data.body_size;
        _offset = 0;
        _state = State::write_audio;
    }
    else {
        _state = State::failed;
        _close_file();
    }
}

void Card::write_audio() 
{
    if (_state != State::write_audio) {
        return;
    }
    if (_offset >= _size) {
        _notify_finish_processing = true;
        _state = State::idle;
        _close_file();
        return;
    }
    
    uint32_t byteswritten;
    auto write_len = std::min(_size - _offset, kChunk);
    if (f_write(&_sdfile, &_bytes[_offset], write_len, (UINT*)&byteswritten) != FR_OK) {
        _state = State::failed;
        _close_file();
        return;
    }
    _offset += byteswritten;
}

bool Card::read_file(const char* path, uint8_t*& out_data, size_t* out_size)
{
    if (_state != State::idle) return false;

    _state = State::read_file;
    if (f_open(&_sdfile, path, FA_OPEN_EXISTING | FA_READ) != FR_OK) {
        _state = State::idle;
        return false;
    }
    
    if (f_read(&_sdfile, _buffer, kChunk, out_size) != FR_OK) {
        _state = State::idle;
        _close_file();
        return false;
    }
    
    _close_file();
    _state = State::idle;

    out_data = _buffer;
    
    return true;
}

bool Card::write_file(const char* path, const uint8_t* in_data, const size_t in_size)
{
    if (_state != State::idle) return false;

    _state = State::write_file;
    if (f_open(&_sdfile, path, FA_CREATE_ALWAYS | FA_WRITE) != FR_OK) {
        _state = State::idle;
        return false;
    }
    
    uint32_t byteswritten;
    if (f_write(&_sdfile, in_data, in_size, (UINT*)&byteswritten) != FR_OK) {
        _close_file();
        _state = State::idle;
        return false;
    }

    _close_file();
    _state = State::idle;
    
    return true;
}

void Card::_close_file()
{
    f_close(&_sdfile);
}

void Card::recognize()
{
    /* Init handler */
    SdmmcHandler::Config sd_cfg;
    sd_cfg.Defaults();
    sd_cfg.speed = SdmmcHandler::Speed::MEDIUM_SLOW;
    sd_cfg.width = SdmmcHandler::BusWidth::BITS_1;
    _sd.Init(sd_cfg);

    /* Links libdaisy i/o to fatfs driver. */
    _fsi.Init(FatFSInterface::Config::MEDIA_SD);

    _state = State::mounting;
}

bool Card::mount()
{
    /* Mount the card */
    auto path = _fsi.GetSDPath();
    if (f_mount(&_fsi.GetSDFileSystem(), path, 1) != FR_OK) {
        return false;
    }
    _state = State::idle;
    return true;
}

void Card::unmount()
{
    auto path = _fsi.GetSDPath();
    f_mount(NULL, path, 0);
    _fsi.DeInit();
    _state = State::unmounted;
}

bool Card::notify_finish_processing()
{
    auto did_finish = _notify_finish_processing;
    _notify_finish_processing = false;
    return did_finish;
}

void Card::cancel()
{
    _close_file();
    _state = State::idle;
}