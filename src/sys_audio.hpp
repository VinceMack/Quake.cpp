// sys_audio.hpp -- Subsystem Audio Umbrella Header
#pragma once

#include "sys_core.hpp"
#include "audio/audio_types.hpp"
#include "audio/audio_dma.hpp"
#include "audio/audio_wav.hpp"
#include "audio/audio_mix.hpp"
#include "audio/audio_main.hpp"

using Audio::portable_samplepair_t;
using Audio::sfx_s;
using Audio::sfx_t;
using Audio::sfxcache_t;
using Audio::dma_t;
using Audio::channel_t;
using Audio::wavinfo_t;
