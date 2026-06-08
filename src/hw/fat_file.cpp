// Compiled into the firmware only for the streaming `tape` engine (it lives in the src/hw wildcard, so
// the guard keeps every other engine's build byte-identical).
#if defined(SPK_ENGINE_TAPE)

#include "fat_file.h"

#include <cstring>

// Size-optimize: main-loop SD glue, never the audio path.
#pragma GCC optimize("Os")

using namespace spotykach;
using namespace daisy;

bool FatFile::open_read(const char* path) {
    if (_open) return false;
    if (f_open(&_f, path, FA_OPEN_EXISTING | FA_READ) != FR_OK) return false;
    _open = true;
    return true;
}

bool FatFile::open_write(const char* path) {
    if (_open) return false;
    // Ensure the parent directory exists (single level, e.g. "tapes/") before creating the file.
    // FR_EXIST (already there) is fine - we ignore the result.
    const char* slash = std::strrchr(path, '/');
    if (slash && slash != path) {
        char dir[32];
        size_t n = static_cast<size_t>(slash - path);
        if (n < sizeof(dir)) { std::memcpy(dir, path, n); dir[n] = '\0'; f_mkdir(dir); }
    }
    if (f_open(&_f, path, FA_CREATE_ALWAYS | FA_WRITE) != FR_OK) return false;
    _open = true;
    return true;
}

void FatFile::close() {
    if (_open) { f_close(&_f); _open = false; }
}

uint32_t FatFile::read(void* dst, uint32_t n) {
    if (!_open) return 0;
    UINT br = 0;
    if (f_read(&_f, dst, n, &br) != FR_OK) return 0;
    return br;
}

uint32_t FatFile::write(const void* src, uint32_t n) {
    if (!_open) return 0;
    UINT bw = 0;
    if (f_write(&_f, src, n, &bw) != FR_OK) return 0;
    return bw;
}

bool FatFile::seek(uint32_t pos) {
    if (!_open) return false;
    return f_lseek(&_f, pos) == FR_OK;
}

#endif // SPK_ENGINE_TAPE
