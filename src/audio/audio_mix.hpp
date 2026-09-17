// audio_mix.hpp -- Software Audio Mixer and Channel Painting
#pragma once

#include "audio/audio_types.hpp"

namespace Audio {

inline constexpr int PAINTBUFFER_SIZE = 512;

extern std::array<portable_samplepair_t, PAINTBUFFER_SIZE> paintbuffer;
extern std::array<std::array<int, 256>, 32> snd_scaletable;

void SND_InitScaletable();
void SND_PaintChannelFrom8(channel_t* ch, sfxcache_t* sc, int count, int offset);
void SND_PaintChannelFrom16(channel_t* ch, sfxcache_t* sc, int count, int offset);
void S_TransferPaintBuffer(int endtime);
void S_PaintChannels(int endtime);

} // namespace Audio
