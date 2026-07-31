// sys_audio.cpp -- Subsystem Audio Implementation
#pragma warning(disable: 4324)

#include "quakedef.hpp"
#include <SDL.h>
#include <EASTL/span.h>
#include <EASTL/array.h>
#include <EASTL/fixed_vector.h>
#include <EASTL/string_view.h>
#include <EASTL/string.h>
#include <EASTL/numeric_limits.h>
#include <EASTL/algorithm.h>
#include <EASTL/numeric.h>
#include <cstring>
#include <cstdio>
#include <bit>
#include <random>
#include <charconv>
#include <utility>

using namespace Client; using namespace Common; using namespace Console; using namespace Render;
using namespace Draw; using namespace Host; using namespace Input; using namespace Keys;
using namespace Math; using namespace Menu; using namespace Model; using namespace Net;
using namespace VM; using namespace Sbar; using namespace Screen; using namespace Server;
using namespace Vid; using namespace View; using namespace Wad;
using namespace Cvar; using namespace Cmd;

namespace Audio {

enum class AudioCommandType { StartSound, StaticSound, StopSound, StopAllSounds, ListenerUpdate, ClearBuffer };

struct AudioCommand {
    AudioCommandType type{};
    int entnum{}, entchannel{};
    sfx_t* sfx{};
    Vector3 origin{};
    float vol{}, attenuation{};
    bool clear{};
    Vector3 v_forward{}, v_right{}, v_up{};
    eastl::array<int, NUM_AMBIENTS> ambient_vols{};
    float host_frametime{}, ambient_fade{};
    bool snd_ambient{};
    int random_offset{};
};

template <typename T, size_t Capacity>
class SPSCQueue {
    static_assert((Capacity & (Capacity - 1)) == 0 && std::is_trivially_copyable_v<T>);
    eastl::array<T, Capacity> buffer_;
    alignas(64) std::atomic<size_t> write_idx_{0}, read_idx_{0};
public:
    [[nodiscard]] bool Push(const T& val) {
        size_t w = write_idx_.load(std::memory_order_relaxed), r = read_idx_.load(std::memory_order_acquire);
        if (w - r >= Capacity) return false;
        buffer_[w & (Capacity - 1)] = val;
        write_idx_.store(w + 1, std::memory_order_release);
        return true;
    }
    [[nodiscard]] bool Pop(T& val) {
        size_t r = read_idx_.load(std::memory_order_relaxed), w = write_idx_.load(std::memory_order_acquire);
        if (r == w) return false;
        val = buffer_[r & (Capacity - 1)];
        read_idx_.store(r + 1, std::memory_order_release);
        return true;
    }
};

SPSCQueue<AudioCommand, 256> command_queue;
float local_volume = 0.7f;
dma_t the_shm;

inline void PushAudioCommand(const AudioCommand& cmd) {
    if (!command_queue.Push(cmd)) Con_Printf("WARNING: Audio command queue overflow!\n");
}

void S_StartSoundInternal(int entnum, int entchannel, sfx_t* sfx, const Vector3& origin, float fvol, float attenuation, int random_offset);
void S_StaticSoundInternal(sfx_t* sfx, const Vector3& origin, float vol, float attenuation);
void S_StopSoundInternal(int entnum, int entchannel);
void S_StopAllSoundsInternal(bool clear);
void S_UpdateInternal(const Vector3& origin, const Vector3& forward, const Vector3& right, const Vector3& up,
                      float vol_val, const eastl::array<int, NUM_AMBIENTS>& ambient_vols,
                      float host_frametime_val, float ambient_fade_val, bool snd_ambient_val);
void ExecuteAudioCommand(const AudioCommand& cmd);

[[nodiscard]] inline short clamp_short(int val) { return static_cast<short>(eastl::clamp(val, -32768, 32767)); }

void S_Play(); void S_PlayVol(); void S_SoundList();

eastl::array<channel_t, MAX_CHANNELS> channels;
std::atomic<int> total_channels{MAX_DYNAMIC_CHANNELS + NUM_AMBIENTS};
bool snd_ambient = true, sound_started = false, fakedma = false, snd_initialized = false;
Vector3 listener_origin, listener_forward, listener_right, listener_up;
int paintedtime;
dma_t* shm = nullptr;

constexpr size_t MAX_SFX = 512;
eastl::fixed_vector<sfx_t, MAX_SFX, false> known_sfx;
eastl::array<sfx_t*, NUM_AMBIENTS> ambient_sfx;

constexpr int desired_speed = 11025, desired_bits = 16;
cvar_t nosound = {"nosound", "0", {}, {}, {}, {}}, precache = {"precache", "1", {}, {}, {}, {}}, bgmbuffer = {"bgmbuffer", "4096", {}, {}, {}, {}},
       ambient_level = {"ambient_level", "0.3", {}, {}, {}, {}}, ambient_fade = {"ambient_fade", "100", {}, {}, {}, {}},
       snd_noextraupdate = {"snd_noextraupdate", "0", {}, {}, {}, {}}, snd_show = {"snd_show", "0", {}, {}, {}, {}},
       _snd_mixahead = {"_snd_mixahead", "0.1", true, {}, {}, {}};

void S_SoundInfo_f() {
    if (!sound_started || !shm) { Con_Printf("sound system not started\n"); return; }
    Con_Printf("%5d stereo\n%5d samples\n%5d samplepos\n%5d samplebits\n%5d submission_chunk\n%5d speed\n0x%x dma buffer\n%5d total_channels\n",
               shm->channels.load() - 1, shm->samples.load(), shm->samplepos.load(), shm->samplebits.load(),
               shm->submission_chunk.load(), shm->speed.load(), shm->buffer.load(), total_channels.load(std::memory_order_relaxed));
}

int snd_blocked = 0;
vec_t sound_nominal_clip_dist = 1000.0;
cvar_t bgmvolume = {"bgmvolume", "1", true, {}, {}, {}}, volume = {"volume", "0.7", true, {}, {}, {}}, loadas8bit = {"loadas8bit", "0", {}, {}, {}, {}};

void S_Startup() {
    if (snd_initialized && !(sound_started = fakedma || SNDDMA_Init())) Con_Printf("S_Startup: SNDDMA_Init failed.\n");
}

void S_Init() {
    Con_Printf("\nSound Initialization\n");
    if (COM_CheckParm("-nosound")) return;
    if (COM_CheckParm("-simsound")) fakedma = true;

    Cmd::AddCommand("play", S_Play);
    Cmd::AddCommand("playvol", S_PlayVol);
    Cmd::AddCommand("soundlist", S_SoundList);
    Cmd::AddCommand("soundinfo", S_SoundInfo_f);
    Cmd::AddCommand("stopsound", []() { S_StopAllSounds(true); });

    for (auto* c : {&nosound, &volume, &precache, &loadas8bit, &bgmvolume, &bgmbuffer, &ambient_level, &ambient_fade, &snd_noextraupdate, &snd_show, &_snd_mixahead})
        Cvar::Register(c);

    if (host_parms.memsize < 0x800000) { Cvar::Set("loadas8bit", "1"); Con_Printf("loading all sounds as 8bit\n"); }
    snd_initialized = true; S_Startup(); SND_InitScaletable(); known_sfx.clear();

    if (fakedma) {
        shm = &the_shm;
        shm->Reset(16, 22050, 2, 32768, static_cast<unsigned char*>(Hunk_Alloc(1 << 16, "shmbuf")));
    }
    if (shm) Con_Printf("Sound sampling rate: %i\n", shm->speed.load());
    ambient_sfx[AMBIENT_WATER] = S_PrecacheSound("ambience/water1.wav");
    ambient_sfx[AMBIENT_SKY] = S_PrecacheSound("ambience/wind2.wav");
    S_StopAllSounds(true);
}

void S_Shutdown() {
    if (!sound_started) return;
    if (shm) shm->gamealive.store(0, std::memory_order_release);
    shm = nullptr; sound_started = false;
    if (!fakedma) SNDDMA_Shutdown();
}

[[nodiscard]] sfx_t* S_FindName(eastl::string_view name) {
    if (name.empty()) Sys_Error("S_FindName: NULL\n");
    if (name.length() >= MAX_QPATH) Sys_Error("Sound name too long: %.*s", static_cast<int>(name.length()), name.data());
    auto it = eastl::find_if(known_sfx.begin(), known_sfx.end(), [name](const sfx_t& s) { return eastl::string_view(s.name) == name; });
    if (it != known_sfx.end()) return it;
    if (known_sfx.full()) Sys_Error("S_FindName: out of sfx_t");
    sfx_t& new_sfx = known_sfx.push_back(); new_sfx = {};
    name.copy(new_sfx.name, name.length());
    return &new_sfx;
}

void S_TouchSound(eastl::string_view name) { if (sound_started) Cache_Check(&S_FindName(name)->cache); }

sfx_t* S_PrecacheSound(eastl::string_view name) {
    if (!sound_started || nosound.value) return nullptr;
    sfx_t* sfx = S_FindName(name);
    if (precache.value) static_cast<void>(S_LoadSound(sfx));
    return sfx;
}

channel_t* SND_PickChannel(int entnum, int entchannel) {
    channel_t* first_to_die = nullptr; int life_left = eastl::numeric_limits<int>::max();
    for (auto& chan : eastl::span(channels).subspan(NUM_AMBIENTS, MAX_DYNAMIC_CHANNELS)) {
        if (entchannel != 0 && chan.entnum == entnum && (chan.entchannel == entchannel || entchannel == -1))
            return (chan.sfx = nullptr, &chan);
        if (chan.entnum == cl.viewentity && entnum != cl.viewentity && chan.sfx) continue;
        if (int remaining = chan.end - paintedtime; remaining < life_left) { life_left = remaining; first_to_die = &chan; }
    }
    if (first_to_die) first_to_die->sfx = nullptr;
    return first_to_die;
}

void SND_Spatialize(channel_t* ch) {
    if (ch->entnum == cl.viewentity) { ch->leftvol = ch->rightvol = ch->master_vol; return; }
    Vector3 source_vec = ch->origin - listener_origin;
    vec_t dist = source_vec.normalize() * ch->dist_mult, dot = listener_right.dot(source_vec);
    bool mono = (shm->channels.load(std::memory_order_relaxed) == 1);
    ch->rightvol = eastl::max(0, static_cast<int>(ch->master_vol * (1.0 - dist) * (mono ? 1.0 : 1.0 + dot)));
    ch->leftvol  = eastl::max(0, static_cast<int>(ch->master_vol * (1.0 - dist) * (mono ? 1.0 : 1.0 - dot)));
}

void S_StartSoundInternal(int entnum, int entchannel, sfx_t* sfx, const Vector3& origin, float fvol, float attenuation, int random_offset) {
    channel_t* target = SND_PickChannel(entnum, entchannel);
    if (!target) return;
    *target = { .sfx = nullptr, .entnum = entnum, .entchannel = entchannel, .origin = origin, .dist_mult = attenuation / sound_nominal_clip_dist, .master_vol = static_cast<int>(fvol * 255) };
    SND_Spatialize(target);
    sfxcache_t* sc = static_cast<sfxcache_t*>(Cache_Check(&sfx->cache));
    if ((!target->leftvol && !target->rightvol) || !sc) return;
    target->sfx = sfx; target->pos = 0; target->end = paintedtime + sc->length;
    for (auto& check : eastl::span(channels).subspan(NUM_AMBIENTS, MAX_DYNAMIC_CHANNELS)) {
        if (&check != target && check.sfx == sfx && !check.pos) {
            int skip = eastl::clamp(random_offset, 0, target->end - 1);
            target->pos += skip; target->end -= skip; break;
        }
    }
}

void S_StopSoundInternal(int entnum, int entchannel) {
    auto active = eastl::span(channels).first(MAX_DYNAMIC_CHANNELS);
    if (auto it = eastl::find_if(active.begin(), active.end(), [=](const channel_t& c) { return c.entnum == entnum && c.entchannel == entchannel; }); it != active.end())
        *it = {};
}

void S_StopAllSoundsInternal(bool) { total_channels = MAX_DYNAMIC_CHANNELS + NUM_AMBIENTS; channels.fill({}); }

void S_StaticSoundInternal(sfx_t* sfx, const Vector3& origin, float vol, float attenuation) {
    if (total_channels == MAX_CHANNELS) return;
    auto* sc = static_cast<sfxcache_t*>(Cache_Check(&sfx->cache));
    if (!sc || sc->loopstart == -1) return;
    channel_t& ss = channels[total_channels++];
    ss = { .sfx = sfx, .end = paintedtime + sc->length, .origin = origin, .dist_mult = (attenuation / 64) / sound_nominal_clip_dist, .master_vol = static_cast<int>(vol) };
    SND_Spatialize(&ss);
}

void ExecuteAudioCommand(const AudioCommand& cmd) {
    switch (cmd.type) {
    case AudioCommandType::StartSound: S_StartSoundInternal(cmd.entnum, cmd.entchannel, cmd.sfx, cmd.origin, cmd.vol, cmd.attenuation, cmd.random_offset); break;
    case AudioCommandType::StaticSound: S_StaticSoundInternal(cmd.sfx, cmd.origin, cmd.vol, cmd.attenuation); break;
    case AudioCommandType::StopSound: S_StopSoundInternal(cmd.entnum, cmd.entchannel); break;
    case AudioCommandType::StopAllSounds: S_StopAllSoundsInternal(cmd.clear); break;
    case AudioCommandType::ListenerUpdate: S_UpdateInternal(cmd.origin, cmd.v_forward, cmd.v_right, cmd.v_up, cmd.vol, cmd.ambient_vols, cmd.host_frametime, cmd.ambient_fade, cmd.snd_ambient); break;
    case AudioCommandType::ClearBuffer: break;
    }
}

void S_StartSound(int entnum, int entchannel, sfx_t* sfx, const Vector3& origin, float fvol, float attenuation) {
    if (!sound_started || !sfx || nosound.value || !Cache_Check(&sfx->cache)) return;
    int rand_off = 0;
    if (shm) {
        if (int max_skip = static_cast<int>(0.1 * shm->speed.load(std::memory_order_relaxed)); max_skip > 0) {
            thread_local std::mt19937 gen(std::random_device{}());
            rand_off = std::uniform_int_distribution<int>(0, max_skip - 1)(gen);
        }
    }
    PushAudioCommand({ .type = AudioCommandType::StartSound, .entnum = entnum, .entchannel = entchannel, .sfx = sfx, .origin = origin, .vol = fvol, .attenuation = attenuation, .random_offset = rand_off });
}

void S_StaticSound(sfx_t* sfx, const Vector3& origin, float vol, float attenuation) {
    if (sound_started && sfx && Cache_Check(&sfx->cache)) PushAudioCommand({ .type = AudioCommandType::StaticSound, .sfx = sfx, .origin = origin, .vol = vol, .attenuation = attenuation });
}

void S_StopSound(int entnum, int entchannel) { if (sound_started) PushAudioCommand({ .type = AudioCommandType::StopSound, .entnum = entnum, .entchannel = entchannel }); }
void S_StopAllSounds(bool clear) { if (sound_started) PushAudioCommand({ .type = AudioCommandType::StopAllSounds, .clear = clear }); }
void S_ClearBuffer() { if (sound_started) PushAudioCommand({ .type = AudioCommandType::ClearBuffer }); }

void S_UpdateInternal(const Vector3& origin, const Vector3& forward, const Vector3& right, const Vector3& up,
                      float vol_val, const eastl::array<int, NUM_AMBIENTS>& ambient_vols,
                      float host_frametime_val, float ambient_fade_val, bool snd_ambient_val) {
    listener_origin = origin; listener_forward = forward; listener_right = right; listener_up = up; local_volume = vol_val;
    for (int i = 0; i < NUM_AMBIENTS; ++i) {
        auto& chan = channels[i];
        if (!snd_ambient_val) { chan.sfx = nullptr; continue; }
        chan.sfx = ambient_sfx[i];
        int target = ambient_vols[i], delta = static_cast<int>(host_frametime_val * ambient_fade_val);
        chan.master_vol = (chan.master_vol < target) ? eastl::min(target, chan.master_vol + delta) : eastl::max(target, chan.master_vol - delta);
        chan.leftvol = chan.rightvol = chan.master_vol;
    }
    const int static_start = NUM_AMBIENTS + MAX_DYNAMIC_CHANNELS;
    auto active_chans = eastl::span(channels).subspan(NUM_AMBIENTS, total_channels - NUM_AMBIENTS);
    for (size_t idx = 0; idx < active_chans.size(); ++idx) {
        auto& ch = active_chans[idx];
        if (!ch.sfx) continue;
        SND_Spatialize(&ch);
        if (!ch.leftvol && !ch.rightvol) continue;
        if (int channel_index = NUM_AMBIENTS + static_cast<int>(idx); channel_index >= static_start) {
            auto static_span = eastl::span(channels).subspan(static_start, channel_index - static_start);
            if (auto match = eastl::find_if(static_span.begin(), static_span.end(), [&ch](const channel_t& o) { return o.sfx == ch.sfx; }); match != static_span.end()) {
                match->leftvol += ch.leftvol; match->rightvol += ch.rightvol; ch.leftvol = ch.rightvol = 0;
            }
        }
    }
    if (fakedma && snd_show.value) {
        Con_Printf("----(%i)----\n", static_cast<int>(eastl::count_if(channels.begin(), channels.begin() + total_channels, [](const channel_t& c) { return c.sfx && (c.leftvol || c.rightvol); })));
    }
}

void S_Update(const Vector3& origin, const Vector3& forward, const Vector3& right, const Vector3& up) {
    if (!sound_started || (snd_blocked > 0)) return;
    AudioCommand cmd{ .type = AudioCommandType::ListenerUpdate, .origin = origin, .vol = volume.value, .v_forward = forward, .v_right = right, .v_up = up,
                      .host_frametime = static_cast<float>(host_frametime), .ambient_fade = ambient_fade.value, .snd_ambient = snd_ambient };
    if (snd_ambient && cl.worldmodel && ambient_level.value) {
        if (mleaf_t* l = Mod_PointInLeaf(origin, cl.worldmodel)) {
            for (int i = 0; i < NUM_AMBIENTS; i++) {
                float vol = ambient_level.value * l->ambient_sound_level[i];
                cmd.ambient_vols[i] = (vol < 8) ? 0 : static_cast<int>(vol);
            }
        }
    }
    PushAudioCommand(cmd);
    if (fakedma) { AudioCommand c{}; while (command_queue.Pop(c)) ExecuteAudioCommand(c); }
}

void S_ExtraUpdate() {}

void S_PlayHelper(bool has_volume) {
    thread_local std::mt19937 rng(std::random_device{}());
    int hash = std::uniform_int_distribution<int>(0, 1000)(rng), step = has_volume ? 2 : 1;
    for (int i = 1; i < Cmd::Argc(); i += step) {
        auto arg = Cmd::Argv(i);
        eastl::string name(arg.data(), arg.length());
        if (arg.find('.') == eastl::string_view::npos) name += ".wav";
        sfx_t* sfx = S_PrecacheSound(name.c_str());
        float vol = 1.0f;
        if (has_volume && i + 1 < Cmd::Argc()) { auto arg_vol = Cmd::Argv(i + 1); std::from_chars(arg_vol.data(), arg_vol.data() + arg_vol.size(), vol); }
        S_StartSound(hash++, 0, sfx, cl_entities[cl.viewentity].origin, vol, 1.0f);
    }
}

void S_Play() { S_PlayHelper(false); }
void S_PlayVol() { S_PlayHelper(true); }

void S_SoundList() {
    int total = 0;
    for (auto& sfx : known_sfx) {
        if (auto* sc = static_cast<sfxcache_t*>(Cache_Check(&sfx.cache))) {
            total += sc->length * sc->width * (sc->stereo + 1);
            Con_Printf("%s%s\n", (sc->loopstart >= 0) ? "L" : " ", sfx.name);
        }
    }
    Con_Printf("Total sound memory: %i\n", total);
}

void S_LocalSound(eastl::string_view sound) {
    if (nosound.value || !sound_started) return;
    sfx_t* sfx = S_FindName(sound);
    if (!sfx || !S_LoadSound(sfx)) {
        if (sfx) Con_Printf("WARNING: S_LocalSound: can't load %.*s\n", static_cast<int>(sound.length()), sound.data());
        return;
    }
    S_StartSound(cl.viewentity, -1, sfx, vec3_origin, 1.0f, 1.0f);
}

template <typename T> [[nodiscard]] constexpr T byteswap(T val) { return (sizeof(T) == 2) ? static_cast<T>(__builtin_bswap16(static_cast<uint16_t>(val))) : (sizeof(T) == 4) ? static_cast<T>(__builtin_bswap32(static_cast<uint32_t>(val))) : val; }

void ResampleSfx(sfx_t* sfx, int inrate, int inwidth, byte* data) {
    auto* sc = static_cast<sfxcache_t*>(Cache_Check(&sfx->cache));
    if (!sc) return;
    float stepscale = static_cast<float>(inrate) / shm->speed.load();
    sc->length = static_cast<int>(sc->length / stepscale);
    if (sc->loopstart != -1) sc->loopstart = static_cast<int>(sc->loopstart / stepscale);
    sc->speed = shm->speed.load(); sc->width = loadas8bit.value ? 1 : inwidth; sc->stereo = 0;

    if (stepscale == 1.0f && inwidth == 1 && sc->width == 1) {
        for (int i = 0; i < sc->length; i++) reinterpret_cast<signed char*>(sc->data)[i] = static_cast<signed char>(data[i] - 128);
    } else {
        int samplefrac = 0, fracstep = static_cast<int>(stepscale * 256);
        for (int i = 0; i < sc->length; i++) {
            int srcsample = samplefrac >> 8; samplefrac += fracstep;
            int sample = 0;
            if (inwidth == 2) {
                short val; std::memcpy(&val, &data[srcsample * 2], sizeof(short));
                if constexpr (std::endian::native == std::endian::big) val = byteswap(val);
                sample = val;
            } else sample = static_cast<int>(data[srcsample] - 128) << 8;

            if (sc->width == 2) { short s = static_cast<short>(sample); std::memcpy(&sc->data[i * sizeof(short)], &s, sizeof(short)); }
            else reinterpret_cast<signed char*>(sc->data)[i] = static_cast<signed char>(sample >> 8);
        }
    }
}

sfxcache_t* S_LoadSound(sfx_t* s) {
    if (auto* sc = static_cast<sfxcache_t*>(Cache_Check(&s->cache))) return sc;
    eastl::array<char, MAX_QPATH + 16> namebuffer;
    std::snprintf(namebuffer.data(), namebuffer.size(), "sound/%s", s->name);
    eastl::array<byte, 1024> stackbuf;
    byte* data = COM_LoadStackFile(namebuffer.data(), stackbuf.data(), sizeof(stackbuf));
    if (!data) { Con_Printf("Couldn't load %s\n", namebuffer.data()); return nullptr; }
    wavinfo_t info = GetWavinfo(s->name, eastl::span<const byte>(data, com_filesize));
    if (info.channels != 1) { Con_Printf("%s is a stereo sample\n", s->name); return nullptr; }
    float stepscale = static_cast<float>(info.rate) / shm->speed.load(std::memory_order_relaxed);
    int len = static_cast<int>(info.samples / stepscale) * info.width * info.channels;
    auto* sc = static_cast<sfxcache_t*>(Cache_Alloc(&s->cache, len + sizeof(sfxcache_t), s->name));
    if (!sc) return nullptr;
    *sc = { .length = info.samples, .loopstart = info.loopstart, .speed = info.rate, .width = info.width, .stereo = info.channels };
    ResampleSfx(s, sc->speed, sc->width, data + info.dataofs);
    return sc;
}

struct WavParser {
    eastl::span<const byte> data;
    size_t iff_offset{0}, chunk_offset{0}, chunk_len{0};
    uint16_t ReadU16(size_t& off) const { uint16_t v = 0; if (off + 2 <= data.size()) { std::memcpy(&v, &data[off], 2); off += 2; } return v; }
    uint32_t ReadU32(size_t& off) const { uint32_t v = 0; if (off + 4 <= data.size()) { std::memcpy(&v, &data[off], 4); off += 4; } return v; }
    bool FindChunk(eastl::string_view tag, bool restart = true) {
        size_t search_off = restart ? iff_offset : chunk_offset + 8 + ((chunk_len + 1) & ~1);
        while (search_off + 8 <= data.size()) {
            size_t off = search_off + 4;
            chunk_len = ReadU32(off); chunk_offset = search_off; search_off += 8 + ((chunk_len + 1) & ~1);
            if (eastl::string_view(reinterpret_cast<const char*>(&data[chunk_offset]), 4) == tag) return true;
        }
        chunk_offset = data.size(); return false;
    }
};

wavinfo_t GetWavinfo(eastl::string_view name, eastl::span<const byte> wav_data) {
    wavinfo_t info{};
    if (wav_data.empty()) return info;
    WavParser parser{wav_data};
    if (!parser.FindChunk("RIFF") || parser.chunk_offset + 12 > wav_data.size() ||
        eastl::string_view(reinterpret_cast<const char*>(&wav_data[parser.chunk_offset + 8]), 4) != "WAVE") {
        Con_Printf("Missing or malformed RIFF/WAVE chunk\n"); return info;
    }
    parser.iff_offset = parser.chunk_offset + 12;
    if (!parser.FindChunk("fmt ")) { Con_Printf("Missing fmt chunk\n"); return info; }
    size_t fmt_off = parser.chunk_offset + 8;
    if (parser.ReadU16(fmt_off) != 1) { Con_Printf("Microsoft PCM format only\n"); return info; }
    info.channels = parser.ReadU16(fmt_off); info.rate = parser.ReadU32(fmt_off); fmt_off += 6; info.width = parser.ReadU16(fmt_off) / 8;
    if (parser.FindChunk("cue ")) {
        size_t cue_off = parser.chunk_offset + 40;
        info.loopstart = parser.ReadU32(cue_off);
        if (parser.FindChunk("LIST", false) && parser.chunk_offset + 40 <= wav_data.size()) {
            if (eastl::string_view(reinterpret_cast<const char*>(&wav_data[parser.chunk_offset + 36]), 4) == "mark") {
                size_t list_off = parser.chunk_offset + 32;
                info.samples = info.loopstart + parser.ReadU32(list_off);
            }
        }
    } else info.loopstart = -1;

    if (!parser.FindChunk("data")) { Con_Printf("Missing data chunk\n"); return info; }
    int samples = static_cast<int>(parser.chunk_len) / info.width;
    if (info.samples) {
        if (samples < info.samples) Sys_Error("Sound %.*s has a bad loop length", static_cast<int>(name.length()), name.data());
    } else info.samples = samples;
    info.dataofs = static_cast<int>(parser.chunk_offset + 8);
    return info;
}

constexpr int PAINTBUFFER_SIZE = 512;
eastl::array<portable_samplepair_t, PAINTBUFFER_SIZE> paintbuffer;
eastl::array<eastl::array<int, 256>, 32> snd_scaletable;

void S_TransferPaintBuffer(int endtime) {
    int samplebits_val = shm->samplebits.load(std::memory_order_relaxed), channels_val = shm->channels.load(std::memory_order_relaxed);
    if (samplebits_val == 16 && channels_val == 2) {
        int lpaintedtime = paintedtime; auto snd_p = reinterpret_cast<const int*>(paintbuffer.data()); int snd_vol = static_cast<int>(local_volume * 256);
        while (lpaintedtime < endtime) {
            int lpos = lpaintedtime & ((shm->samples.load() >> 1) - 1);
            auto snd_out = reinterpret_cast<short*>(shm->buffer.load()) + (lpos << 1);
            int count = eastl::min(endtime - lpaintedtime, (shm->samples.load() >> 1) - lpos) << 1;
            for (int i = 0; i < count; i++) snd_out[i] = clamp_short((snd_p[i] * snd_vol) >> 8);
            snd_p += count; lpaintedtime += (count >> 1);
        }
        return;
    }
    const int* p = reinterpret_cast<const int*>(paintbuffer.data());
    int count = (endtime - paintedtime) * channels_val, out_mask = shm->samples.load(std::memory_order_relaxed) - 1,
        out_idx = (paintedtime * channels_val) & out_mask, step = 3 - channels_val, mix_vol = static_cast<int>(local_volume * 256);
    auto pbuf = static_cast<unsigned char*>(shm->buffer.load());
    if (samplebits_val == 16) {
        auto out = reinterpret_cast<short*>(pbuf);
        while (count--) { out[out_idx] = clamp_short((*p * mix_vol) >> 8); p += step; out_idx = (out_idx + 1) & out_mask; }
    } else if (samplebits_val == 8) {
        while (count--) { int val = clamp_short((*p * mix_vol) >> 8); p += step; pbuf[out_idx] = static_cast<unsigned char>((val >> 8) + 128); out_idx = (out_idx + 1) & out_mask; }
    }
}

void SND_PaintChannelFrom8(channel_t* ch, sfxcache_t* sc, int count, int offset) {
    const int *lscale = snd_scaletable[eastl::min(ch->leftvol, 255) >> 3].data(), *rscale = snd_scaletable[eastl::min(ch->rightvol, 255) >> 3].data();
    auto sfx = static_cast<const unsigned char*>(sc->data) + ch->pos;
    for (int i = 0; i < count; i++) { paintbuffer[offset + i].left += lscale[sfx[i]]; paintbuffer[offset + i].right += rscale[sfx[i]]; }
    ch->pos += count;
}

void SND_PaintChannelFrom16(channel_t* ch, sfxcache_t* sc, int count, int offset) {
    auto samples = reinterpret_cast<const int16_t*>(sc->data) + ch->pos;
    for (int i = 0; i < count; i++) { paintbuffer[offset + i].left += (samples[i] * ch->leftvol) >> 8; paintbuffer[offset + i].right += (samples[i] * ch->rightvol) >> 8; }
    ch->pos += count;
}

void S_PaintChannels(int endtime) {
    while (paintedtime < endtime) {
        int end = eastl::min(endtime, paintedtime + PAINTBUFFER_SIZE);
        paintbuffer.fill({0, 0});
        for (int i = 0; i < total_channels; i++) {
            auto& chan = channels[i];
            if (!chan.sfx || (!chan.leftvol && !chan.rightvol)) continue;
            auto* sc = static_cast<sfxcache_t*>(Cache_Check(&chan.sfx->cache));
            if (!sc) { chan.sfx = nullptr; continue; }
            int ltime = paintedtime;
            while (ltime < end) {
                int count = eastl::min(chan.end, end) - ltime;
                if (count > 0) {
                    if (sc->width == 1) SND_PaintChannelFrom8(&chan, sc, count, ltime - paintedtime);
                    else SND_PaintChannelFrom16(&chan, sc, count, ltime - paintedtime);
                    ltime += count;
                }
                if (ltime >= chan.end) {
                    if (sc->loopstart >= 0) { chan.pos = sc->loopstart; chan.end = ltime + sc->length - chan.pos; }
                    else { chan.sfx = nullptr; break; }
                }
            }
        }
        S_TransferPaintBuffer(end);
        paintedtime = end;
    }
}

void SND_InitScaletable() {
    for (int i = 0; i < 32; i++)
        for (int j = 0; j < 256; j++)
            snd_scaletable[i][j] = static_cast<signed char>(j) * i * 8;
}

int snd_inited;

void paint_audio(void*, Uint8* stream, int len) {
    if (shm) {
        AudioCommand cmd{};
        while (command_queue.Pop(cmd)) ExecuteAudioCommand(cmd);
        shm->buffer.store(stream, std::memory_order_release);
        int samplebits_val = shm->samplebits.load(std::memory_order_relaxed);
        int current_pos = shm->samplepos.load(std::memory_order_acquire);
        int next_pos = current_pos + len / (samplebits_val / 8) / 2;
        shm->samplepos.store(next_pos, std::memory_order_release);
        S_PaintChannels(next_pos);
    }
}

bool SNDDMA_Init() {
    SDL_AudioSpec desired{};
    desired.freq = desired_speed; desired.channels = 2; desired.samples = 512; desired.callback = paint_audio;
    snd_inited = 0;
    if (desired_bits == 8) desired.format = AUDIO_U8;
    else if (desired_bits == 16) desired.format = (SDL_BYTEORDER == SDL_BIG_ENDIAN) ? AUDIO_S16MSB : AUDIO_S16LSB;
    else { Con_Printf("Unknown number of audio bits: %d\n", desired_bits); return false; }

    if (SDL_OpenAudio(&desired, nullptr) < 0) { Con_Printf("Couldn't open SDL audio: %s\n", SDL_GetError()); return false; }
    SDL_PauseAudio(0);
    shm = &the_shm;
    shm->Reset(static_cast<int>(desired.format & 0xFF), desired.freq, desired.channels, desired.samples * desired.channels);
    snd_inited = 1;
    return true;
}

void SNDDMA_Shutdown() {
    if (snd_inited) { SDL_CloseAudio(); snd_inited = 0; }
}

} // namespace Audio
