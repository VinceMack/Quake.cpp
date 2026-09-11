// audio_main.cpp -- Audio Subsystem Orchestration, Spatialization, and Lifecycle Implementation
#include "quakedef.hpp"
#include "audio/audio_main.hpp"
#include "audio/audio_dma.hpp"
#include "audio/audio_wav.hpp"
#include "audio/audio_mix.hpp"

#include <random>
#include <charconv>
#include <EASTL/algorithm.h>
#include <EASTL/numeric_limits.h>

using namespace Common;
using namespace Console;
using namespace Cvar;
using namespace Cmd;
using namespace Client;
using namespace Model;
using namespace Host;
using namespace Math;

namespace Audio {

SPSCQueue<AudioCommand, 256> command_queue;
float local_volume = 0.7f;

eastl::array<channel_t, MAX_CHANNELS> channels;
std::atomic<int> total_channels{MAX_DYNAMIC_CHANNELS + NUM_AMBIENTS};
bool snd_ambient = true, sound_started = false, fakedma = false, snd_initialized = false;
Vector3 listener_origin, listener_forward, listener_right, listener_up;
int paintedtime = 0;

eastl::fixed_vector<sfx_t, MAX_SFX, false> known_sfx;
eastl::array<sfx_t*, NUM_AMBIENTS> ambient_sfx;

int snd_blocked = 0;
vec_t sound_nominal_clip_dist = 1000.0;

cvar_t nosound = {"nosound", "0", {}, {}, {}, {}};
cvar_t precache = {"precache", "1", {}, {}, {}, {}};
cvar_t bgmbuffer = {"bgmbuffer", "4096", {}, {}, {}, {}};
cvar_t ambient_level = {"ambient_level", "0.3", {}, {}, {}, {}};
cvar_t ambient_fade = {"ambient_fade", "100", {}, {}, {}, {}};
cvar_t snd_noextraupdate = {"snd_noextraupdate", "0", {}, {}, {}, {}};
cvar_t snd_show = {"snd_show", "0", {}, {}, {}, {}};
cvar_t _snd_mixahead = {"_snd_mixahead", "0.1", true, {}, {}, {}};
cvar_t bgmvolume = {"bgmvolume", "1", true, {}, {}, {}};
cvar_t volume = {"volume", "0.7", true, {}, {}, {}};
cvar_t loadas8bit = {"loadas8bit", "0", {}, {}, {}, {}};

void PushAudioCommand(const AudioCommand& cmd) {
    if (!command_queue.Push(cmd)) {
        Con_Printf("WARNING: Audio command queue overflow!\n");
    }
}

void S_SoundInfo_f() {
    if (!sound_started || !shm) {
        Con_Printf("sound system not started\n");
        return;
    }
    Con_Printf("%5d stereo\n%5d samples\n%5d samplepos\n%5d samplebits\n%5d submission_chunk\n%5d speed\n0x%x dma buffer\n%5d total_channels\n",
               shm->channels.load() - 1, shm->samples.load(), shm->samplepos.load(), shm->samplebits.load(),
               shm->submission_chunk.load(), shm->speed.load(), shm->buffer.load(), total_channels.load(std::memory_order_relaxed));
}

void S_Startup() {
    if (snd_initialized && !(sound_started = fakedma || SNDDMA_Init())) {
        Con_Printf("S_Startup: SNDDMA_Init failed.\n");
    }
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

    for (auto* c : {&nosound, &volume, &precache, &loadas8bit, &bgmvolume, &bgmbuffer,
                    &ambient_level, &ambient_fade, &snd_noextraupdate, &snd_show, &_snd_mixahead}) {
        Cvar::Register(c);
    }

    if (host_parms.memsize < 0x800000) {
        Cvar::Set("loadas8bit", "1");
        Con_Printf("loading all sounds as 8bit\n");
    }
    snd_initialized = true;
    S_Startup();
    SND_InitScaletable();
    known_sfx.clear();

    if (fakedma) {
        shm = &the_shm;
        shm->Reset(16, 22050, 2, 32768, static_cast<unsigned char*>(Hunk_Alloc(1 << 16, "shmbuf")));
    }
    if (shm) Con_Printf("Sound sampling rate: %i\n", shm->speed.load());
    ambient_sfx[AMBIENT_WATER] = S_PrecacheSound("ambience/water1.wav");
    ambient_sfx[AMBIENT_SKY]   = S_PrecacheSound("ambience/wind2.wav");
    S_StopAllSounds(true);
}

void S_Shutdown() {
    if (!sound_started) return;
    if (shm) shm->gamealive.store(0, std::memory_order_release);
    shm = nullptr;
    sound_started = false;
    if (!fakedma) SNDDMA_Shutdown();
}

sfx_t* S_FindName(eastl::string_view name) {
    if (name.empty()) Sys_Error("S_FindName: NULL\n");
    if (name.length() >= MAX_QPATH) {
        Sys_Error("Sound name too long: %.*s", static_cast<int>(name.length()), name.data());
    }
    auto it = eastl::find_if(known_sfx.begin(), known_sfx.end(), [name](const sfx_t& s) {
        return eastl::string_view(s.name) == name;
    });
    if (it != known_sfx.end()) return it;
    if (known_sfx.full()) Sys_Error("S_FindName: out of sfx_t");
    sfx_t& new_sfx = known_sfx.push_back();
    new_sfx = {};
    name.copy(new_sfx.name, name.length());
    return &new_sfx;
}

void S_TouchSound(eastl::string_view name) {
    if (sound_started) Cache_Check(&S_FindName(name)->cache);
}

sfx_t* S_PrecacheSound(eastl::string_view name) {
    if (!sound_started || nosound.value) return nullptr;
    sfx_t* sfx = S_FindName(name);
    if (precache.value) static_cast<void>(S_LoadSound(sfx));
    return sfx;
}

channel_t* SND_PickChannel(int entnum, int entchannel) {
    channel_t* first_to_die = nullptr;
    int life_left = eastl::numeric_limits<int>::max();
    for (auto& chan : eastl::span(channels).subspan(NUM_AMBIENTS, MAX_DYNAMIC_CHANNELS)) {
        if (entchannel != 0 && chan.entnum == entnum && (chan.entchannel == entchannel || entchannel == -1)) {
            chan.sfx = nullptr;
            return &chan;
        }
        if (chan.entnum == cl.viewentity && entnum != cl.viewentity && chan.sfx) continue;
        if (int remaining = chan.end - paintedtime; remaining < life_left) {
            life_left = remaining;
            first_to_die = &chan;
        }
    }
    if (first_to_die) first_to_die->sfx = nullptr;
    return first_to_die;
}

void SND_Spatialize(channel_t* ch) {
    if (ch->entnum == cl.viewentity) {
        ch->leftvol = ch->rightvol = ch->master_vol;
        return;
    }
    Vector3 source_vec = ch->origin - listener_origin;
    vec_t dist = source_vec.normalize() * ch->dist_mult;
    vec_t dot = listener_right.dot(source_vec);
    bool mono = (shm->channels.load(std::memory_order_relaxed) == 1);
    ch->rightvol = eastl::max(0, static_cast<int>(ch->master_vol * (1.0 - dist) * (mono ? 1.0 : 1.0 + dot)));
    ch->leftvol  = eastl::max(0, static_cast<int>(ch->master_vol * (1.0 - dist) * (mono ? 1.0 : 1.0 - dot)));
}

void S_StartSoundInternal(int entnum, int entchannel, sfx_t* sfx, const Vector3& origin, float fvol, float attenuation, int random_offset) {
    channel_t* target = SND_PickChannel(entnum, entchannel);
    if (!target) return;
    *target = { .sfx = nullptr, .entnum = entnum, .entchannel = entchannel, .origin = origin,
                .dist_mult = attenuation / sound_nominal_clip_dist, .master_vol = static_cast<int>(fvol * 255) };
    SND_Spatialize(target);
    sfxcache_t* sc = static_cast<sfxcache_t*>(Cache_Check(&sfx->cache));
    if ((!target->leftvol && !target->rightvol) || !sc) return;
    target->sfx = sfx;
    target->pos = 0;
    target->end = paintedtime + sc->length;
    for (auto& check : eastl::span(channels).subspan(NUM_AMBIENTS, MAX_DYNAMIC_CHANNELS)) {
        if (&check != target && check.sfx == sfx && !check.pos) {
            int skip = eastl::clamp(random_offset, 0, target->end - 1);
            target->pos += skip;
            target->end -= skip;
            break;
        }
    }
}

void S_StopSoundInternal(int entnum, int entchannel) {
    auto active = eastl::span(channels).first(MAX_DYNAMIC_CHANNELS);
    if (auto it = eastl::find_if(active.begin(), active.end(), [=](const channel_t& c) {
        return c.entnum == entnum && c.entchannel == entchannel;
    }); it != active.end()) {
        *it = {};
    }
}

void S_StopAllSoundsInternal(bool) {
    total_channels = MAX_DYNAMIC_CHANNELS + NUM_AMBIENTS;
    channels.fill({});
}

void S_StaticSoundInternal(sfx_t* sfx, const Vector3& origin, float vol, float attenuation) {
    if (total_channels == MAX_CHANNELS) return;
    auto* sc = static_cast<sfxcache_t*>(Cache_Check(&sfx->cache));
    if (!sc || sc->loopstart == -1) return;
    channel_t& ss = channels[total_channels++];
    ss = { .sfx = sfx, .end = paintedtime + sc->length, .origin = origin,
           .dist_mult = (attenuation / 64) / sound_nominal_clip_dist, .master_vol = static_cast<int>(vol) };
    SND_Spatialize(&ss);
}

void ExecuteAudioCommand(const AudioCommand& cmd) {
    switch (cmd.type) {
    case AudioCommandType::StartSound:
        S_StartSoundInternal(cmd.entnum, cmd.entchannel, cmd.sfx, cmd.origin, cmd.vol, cmd.attenuation, cmd.random_offset);
        break;
    case AudioCommandType::StaticSound:
        S_StaticSoundInternal(cmd.sfx, cmd.origin, cmd.vol, cmd.attenuation);
        break;
    case AudioCommandType::StopSound:
        S_StopSoundInternal(cmd.entnum, cmd.entchannel);
        break;
    case AudioCommandType::StopAllSounds:
        S_StopAllSoundsInternal(cmd.clear);
        break;
    case AudioCommandType::ListenerUpdate:
        S_UpdateInternal(cmd.origin, cmd.v_forward, cmd.v_right, cmd.v_up, cmd.vol, cmd.ambient_vols,
                         cmd.host_frametime, cmd.ambient_fade, cmd.snd_ambient);
        break;
    case AudioCommandType::ClearBuffer:
        break;
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
    PushAudioCommand({ .type = AudioCommandType::StartSound, .entnum = entnum, .entchannel = entchannel,
                       .sfx = sfx, .origin = origin, .vol = fvol, .attenuation = attenuation, .random_offset = rand_off });
}

void S_StaticSound(sfx_t* sfx, const Vector3& origin, float vol, float attenuation) {
    if (sound_started && sfx && Cache_Check(&sfx->cache)) {
        PushAudioCommand({ .type = AudioCommandType::StaticSound, .sfx = sfx, .origin = origin, .vol = vol, .attenuation = attenuation });
    }
}

void S_StopSound(int entnum, int entchannel) {
    if (sound_started) PushAudioCommand({ .type = AudioCommandType::StopSound, .entnum = entnum, .entchannel = entchannel });
}

void S_StopAllSounds(bool clear) {
    if (sound_started) PushAudioCommand({ .type = AudioCommandType::StopAllSounds, .clear = clear });
}

void S_ClearBuffer() {
    if (sound_started) PushAudioCommand({ .type = AudioCommandType::ClearBuffer });
}

void S_UpdateInternal(const Vector3& origin, const Vector3& forward, const Vector3& right, const Vector3& up,
                      float vol_val, const eastl::array<int, NUM_AMBIENTS>& ambient_vols,
                      float host_frametime_val, float ambient_fade_val, bool snd_ambient_val) {
    listener_origin = origin;
    listener_forward = forward;
    listener_right = right;
    listener_up = up;
    local_volume = vol_val;

    for (int i = 0; i < NUM_AMBIENTS; ++i) {
        auto& chan = channels[i];
        if (!snd_ambient_val) { chan.sfx = nullptr; continue; }
        chan.sfx = ambient_sfx[i];
        int target = ambient_vols[i];
        int delta = static_cast<int>(host_frametime_val * ambient_fade_val);
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
                match->leftvol += ch.leftvol;
                match->rightvol += ch.rightvol;
                ch.leftvol = ch.rightvol = 0;
            }
        }
    }
    if (fakedma && snd_show.value) {
        Con_Printf("----(%i)----\n", static_cast<int>(eastl::count_if(channels.begin(), channels.begin() + total_channels, [](const channel_t& c) {
            return c.sfx && (c.leftvol || c.rightvol);
        })));
    }
}

void S_Update(const Vector3& origin, const Vector3& forward, const Vector3& right, const Vector3& up) {
    if (!sound_started || (snd_blocked > 0)) return;
    AudioCommand cmd{ .type = AudioCommandType::ListenerUpdate, .origin = origin, .vol = volume.value,
                      .v_forward = forward, .v_right = right, .v_up = up,
                      .host_frametime = static_cast<float>(host_frametime), .ambient_fade = ambient_fade.value,
                      .snd_ambient = snd_ambient };
    if (snd_ambient && cl.worldmodel && ambient_level.value) {
        if (mleaf_t* l = Mod_PointInLeaf(origin, cl.worldmodel)) {
            for (int i = 0; i < NUM_AMBIENTS; i++) {
                float vol = ambient_level.value * l->ambient_sound_level[i];
                cmd.ambient_vols[i] = (vol < 8) ? 0 : static_cast<int>(vol);
            }
        }
    }
    PushAudioCommand(cmd);
    if (fakedma) {
        AudioCommand c{};
        while (command_queue.Pop(c)) ExecuteAudioCommand(c);
    }
}

void S_ExtraUpdate() {}

void S_PlayHelper(bool has_volume) {
    thread_local std::mt19937 rng(std::random_device{}());
    int hash = std::uniform_int_distribution<int>(0, 1000)(rng);
    int step = has_volume ? 2 : 1;
    for (int i = 1; i < Cmd::Argc(); i += step) {
        auto arg = Cmd::Argv(i);
        eastl::string name(arg.data(), arg.length());
        if (arg.find('.') == eastl::string_view::npos) name += ".wav";
        sfx_t* sfx = S_PrecacheSound(name.c_str());
        float vol = 1.0f;
        if (has_volume && i + 1 < Cmd::Argc()) {
            auto arg_vol = Cmd::Argv(i + 1);
            std::from_chars(arg_vol.data(), arg_vol.data() + arg_vol.size(), vol);
        }
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

} // namespace Audio
