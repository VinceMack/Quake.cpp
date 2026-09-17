// audio_mix.cpp -- Software Audio Mixer and Channel Painting Implementation
#include "quakedef.hpp"
#include "audio/audio_mix.hpp"
#include "audio/audio_dma.hpp"
#include "audio/audio_main.hpp"

#include <algorithm>

using namespace Common;

namespace Audio {

namespace {

[[nodiscard]] inline short clamp_short(int val) {
    return static_cast<short>(std::clamp(val, -32768, 32767));
}

} // anonymous namespace

std::array<portable_samplepair_t, PAINTBUFFER_SIZE> paintbuffer;
std::array<std::array<int, 256>, 32> snd_scaletable;

void S_TransferPaintBuffer(int endtime) {
    int samplebits_val = shm->samplebits.load(std::memory_order_relaxed);
    int channels_val = shm->channels.load(std::memory_order_relaxed);
    if (samplebits_val == 16 && channels_val == 2) {
        int lpaintedtime = paintedtime;
        auto snd_p = reinterpret_cast<const int*>(paintbuffer.data());
        int snd_vol = static_cast<int>(local_volume * 256);
        while (lpaintedtime < endtime) {
            int lpos = lpaintedtime & ((shm->samples.load() >> 1) - 1);
            auto snd_out = reinterpret_cast<short*>(shm->buffer.load()) + (lpos << 1);
            int count = std::min(endtime - lpaintedtime, (shm->samples.load() >> 1) - lpos) << 1;
            for (int i = 0; i < count; i++) {
                snd_out[i] = clamp_short((snd_p[i] * snd_vol) >> 8);
            }
            snd_p += count;
            lpaintedtime += (count >> 1);
        }
        return;
    }
    const int* p = reinterpret_cast<const int*>(paintbuffer.data());
    int count = (endtime - paintedtime) * channels_val;
    int out_mask = shm->samples.load(std::memory_order_relaxed) - 1;
    int out_idx = (paintedtime * channels_val) & out_mask;
    int step = 3 - channels_val;
    int mix_vol = static_cast<int>(local_volume * 256);
    auto pbuf = static_cast<unsigned char*>(shm->buffer.load());
    if (samplebits_val == 16) {
        auto out = reinterpret_cast<short*>(pbuf);
        while (count--) {
            out[out_idx] = clamp_short((*p * mix_vol) >> 8);
            p += step;
            out_idx = (out_idx + 1) & out_mask;
        }
    } else if (samplebits_val == 8) {
        while (count--) {
            int val = clamp_short((*p * mix_vol) >> 8);
            p += step;
            pbuf[out_idx] = static_cast<unsigned char>((val >> 8) + 128);
            out_idx = (out_idx + 1) & out_mask;
        }
    }
}

void SND_PaintChannelFrom8(channel_t* ch, sfxcache_t* sc, int count, int offset) {
    const int *lscale = snd_scaletable[std::min(ch->leftvol, 255) >> 3].data();
    const int *rscale = snd_scaletable[std::min(ch->rightvol, 255) >> 3].data();
    auto sfx = static_cast<const unsigned char*>(sc->data) + ch->pos;
    for (int i = 0; i < count; i++) {
        paintbuffer[offset + i].left += lscale[sfx[i]];
        paintbuffer[offset + i].right += rscale[sfx[i]];
    }
    ch->pos += count;
}

void SND_PaintChannelFrom16(channel_t* ch, sfxcache_t* sc, int count, int offset) {
    auto samples = reinterpret_cast<const int16_t*>(sc->data) + ch->pos;
    for (int i = 0; i < count; i++) {
        paintbuffer[offset + i].left += (samples[i] * ch->leftvol) >> 8;
        paintbuffer[offset + i].right += (samples[i] * ch->rightvol) >> 8;
    }
    ch->pos += count;
}

void S_PaintChannels(int endtime) {
    while (paintedtime < endtime) {
        int end = std::min(endtime, paintedtime + PAINTBUFFER_SIZE);
        paintbuffer.fill({0, 0});
        for (int i = 0; i < total_channels; i++) {
            auto& chan = channels[i];
            if (!chan.sfx || (!chan.leftvol && !chan.rightvol)) continue;
            sfxcache_t* sc = S_SfxCache(chan.sfx);
            if (!sc) { chan.sfx = nullptr; continue; }
            int ltime = paintedtime;
            while (ltime < end) {
                int count = std::min(chan.end, end) - ltime;
                if (count > 0) {
                    if (sc->width == 1) SND_PaintChannelFrom8(&chan, sc, count, ltime - paintedtime);
                    else SND_PaintChannelFrom16(&chan, sc, count, ltime - paintedtime);
                    ltime += count;
                }
                if (ltime >= chan.end) {
                    if (sc->loopstart >= 0) {
                        chan.pos = sc->loopstart;
                        chan.end = ltime + sc->length - chan.pos;
                    } else {
                        chan.sfx = nullptr;
                        break;
                    }
                }
            }
        }
        S_TransferPaintBuffer(end);
        paintedtime = end;
    }
}

void SND_InitScaletable() {
    for (int i = 0; i < 32; i++) {
        for (int j = 0; j < 256; j++) {
            snd_scaletable[i][j] = static_cast<signed char>(j) * i * 8;
        }
    }
}

} // namespace Audio
