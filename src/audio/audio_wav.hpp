// audio_wav.hpp -- RIFF/WAVE Format Parser and Sound Asset Loader
#pragma once

#include "audio/audio_types.hpp"
#include <span>

namespace Audio {

[[nodiscard]] wavinfo_t GetWavinfo(std::string_view name, std::span<const byte> wav_data);
void ResampleSfx(sfx_t* sfx, int inrate, int inwidth, byte* data);
[[nodiscard]] sfxcache_t* S_LoadSound(sfx_t* s);

} // namespace Audio
