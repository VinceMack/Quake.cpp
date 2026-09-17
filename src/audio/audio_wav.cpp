// audio_wav.cpp -- RIFF/WAVE Format Parser and Sound Asset Loader Implementation
#include "quakedef.hpp"
#include "audio/audio_wav.hpp"
#include "audio/audio_dma.hpp"
#include "audio/audio_main.hpp"

#include <cstring>
#include <cstdio>
#include <bit>

using namespace Common;
using namespace Console;

namespace Audio {

namespace {

template <typename T>
[[nodiscard]] constexpr T byteswap(T val) {
    if constexpr (sizeof(T) == 2) {
        return static_cast<T>(__builtin_bswap16(static_cast<uint16_t>(val)));
    } else if constexpr (sizeof(T) == 4) {
        return static_cast<T>(__builtin_bswap32(static_cast<uint32_t>(val)));
    } else {
        return val;
    }
}

struct WavParser {
    std::span<const byte> data;
    size_t iff_offset{0}, chunk_offset{0}, chunk_len{0};

    uint16_t ReadU16(size_t& off) const {
        uint16_t v = 0;
        if (off + 2 <= data.size()) {
            std::memcpy(&v, &data[off], 2);
            off += 2;
        }
        return v;
    }

    uint32_t ReadU32(size_t& off) const {
        uint32_t v = 0;
        if (off + 4 <= data.size()) {
            std::memcpy(&v, &data[off], 4);
            off += 4;
        }
        return v;
    }

    bool FindChunk(std::string_view tag, bool restart = true) {
        size_t search_off = restart ? iff_offset : chunk_offset + 8 + ((chunk_len + 1) & ~1);
        while (search_off + 8 <= data.size()) {
            size_t off = search_off + 4;
            chunk_len = ReadU32(off);
            chunk_offset = search_off;
            search_off += 8 + ((chunk_len + 1) & ~1);
            if (std::string_view(reinterpret_cast<const char*>(&data[chunk_offset]), 4) == tag) {
                return true;
            }
        }
        chunk_offset = data.size();
        return false;
    }
};

} // anonymous namespace

wavinfo_t GetWavinfo(std::string_view name, std::span<const byte> wav_data) {
    wavinfo_t info{};
    if (wav_data.empty()) return info;
    WavParser parser{wav_data};
    if (!parser.FindChunk("RIFF") || parser.chunk_offset + 12 > wav_data.size() ||
        std::string_view(reinterpret_cast<const char*>(&wav_data[parser.chunk_offset + 8]), 4) != "WAVE") {
        Con_Printf("Missing or malformed RIFF/WAVE chunk\n");
        return info;
    }
    parser.iff_offset = parser.chunk_offset + 12;
    if (!parser.FindChunk("fmt ")) {
        Con_Printf("Missing fmt chunk\n");
        return info;
    }
    size_t fmt_off = parser.chunk_offset + 8;
    if (parser.ReadU16(fmt_off) != 1) {
        Con_Printf("Microsoft PCM format only\n");
        return info;
    }
    info.channels = parser.ReadU16(fmt_off);
    info.rate = parser.ReadU32(fmt_off);
    fmt_off += 6;
    info.width = parser.ReadU16(fmt_off) / 8;
    if (parser.FindChunk("cue ")) {
        size_t cue_off = parser.chunk_offset + 40;
        info.loopstart = parser.ReadU32(cue_off);
        if (parser.FindChunk("LIST", false) && parser.chunk_offset + 40 <= wav_data.size()) {
            if (std::string_view(reinterpret_cast<const char*>(&wav_data[parser.chunk_offset + 36]), 4) == "mark") {
                size_t list_off = parser.chunk_offset + 32;
                info.samples = info.loopstart + parser.ReadU32(list_off);
            }
        }
    } else {
        info.loopstart = -1;
    }

    if (!parser.FindChunk("data")) {
        Con_Printf("Missing data chunk\n");
        return info;
    }
    int samples = static_cast<int>(parser.chunk_len) / info.width;
    if (info.samples) {
        if (samples < info.samples) {
            Sys_Error("Sound %.*s has a bad loop length", static_cast<int>(name.length()), name.data());
        }
    } else {
        info.samples = samples;
    }
    info.dataofs = static_cast<int>(parser.chunk_offset + 8);
    return info;
}

void ResampleSfx(sfx_t* sfx, int inrate, int inwidth, byte* data) {
    auto* sc = static_cast<sfxcache_t*>(Cache_Check(&sfx->cache));
    if (!sc) return;
    float stepscale = static_cast<float>(inrate) / shm->speed.load();
    sc->length = static_cast<int>(sc->length / stepscale);
    if (sc->loopstart != -1) sc->loopstart = static_cast<int>(sc->loopstart / stepscale);
    sc->speed = shm->speed.load();
    sc->width = loadas8bit.value ? 1 : inwidth;
    sc->stereo = 0;

    if (stepscale == 1.0f && inwidth == 1 && sc->width == 1) {
        for (int i = 0; i < sc->length; i++) {
            reinterpret_cast<signed char*>(sc->data)[i] = static_cast<signed char>(data[i] - 128);
        }
    } else {
        int samplefrac = 0, fracstep = static_cast<int>(stepscale * 256);
        for (int i = 0; i < sc->length; i++) {
            int srcsample = samplefrac >> 8;
            samplefrac += fracstep;
            int sample = 0;
            if (inwidth == 2) {
                short val;
                std::memcpy(&val, &data[srcsample * 2], sizeof(short));
                if constexpr (std::endian::native == std::endian::big) val = byteswap(val);
                sample = val;
            } else {
                sample = static_cast<int>(data[srcsample] - 128) << 8;
            }

            if (sc->width == 2) {
                short s = static_cast<short>(sample);
                std::memcpy(&sc->data[i * sizeof(short)], &s, sizeof(short));
            } else {
                reinterpret_cast<signed char*>(sc->data)[i] = static_cast<signed char>(sample >> 8);
            }
        }
    }
}

sfxcache_t* S_LoadSound(sfx_t* s) {
    if (auto* sc = static_cast<sfxcache_t*>(Cache_Check(&s->cache))) return sc;
    std::array<char, MAX_QPATH + 16> namebuffer;
    std::snprintf(namebuffer.data(), namebuffer.size(), "sound/%s", s->name);
    std::array<byte, 1024> stackbuf;
    byte* data = COM_LoadStackFile(namebuffer.data(), stackbuf.data(), sizeof(stackbuf));
    if (!data) {
        Con_Printf("Couldn't load %s\n", namebuffer.data());
        return nullptr;
    }
    wavinfo_t info = GetWavinfo(s->name, std::span<const byte>(data, com_filesize));
    if (info.channels != 1) {
        Con_Printf("%s is a stereo sample\n", s->name);
        return nullptr;
    }
    float stepscale = static_cast<float>(info.rate) / shm->speed.load(std::memory_order_relaxed);
    int len = static_cast<int>(info.samples / stepscale) * info.width * info.channels;
    auto* sc = static_cast<sfxcache_t*>(Cache_Alloc(&s->cache, len + sizeof(sfxcache_t), s->name));
    if (!sc) return nullptr;
    *sc = { .length = info.samples, .loopstart = info.loopstart, .speed = info.rate, .width = info.width, .stereo = info.channels };
    ResampleSfx(s, sc->speed, sc->width, data + info.dataofs);
    return sc;
}

} // namespace Audio
