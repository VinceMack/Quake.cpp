// audio.cpp -- merged audio subsystem (snd_*.cpp)
// Contains: sound control, caching, mixing, and SDL audio driver
#pragma warning(disable: 4324)

#include <SDL.h>
#include "quakedef.hpp"
#include <span>
#include <array>
#include <string_view>
#include <cstring>
#include <cstdio>
#include <algorithm>
#include <bit>
#include <limits>
#include <new>
#include <random>
#include <charconv>
#include <cstdint>

using namespace CDAudio;
using namespace Client;
using namespace Common;
using namespace Console;
using namespace Render;
using namespace Draw;
using namespace Host;
using namespace Input;
using namespace Keys;
using namespace Math;
using namespace Menu;
using namespace Model;
using namespace Net;
using namespace VM;
using namespace Sbar;
using namespace Screen;
using namespace Server;
using namespace Audio;
using namespace Vid;
using namespace View;
using namespace Wad;
using namespace Cvar;
using namespace Cmd;


namespace Audio {

namespace {

enum class AudioCommandType {
    StartSound,
    StaticSound,
    StopSound,
    StopAllSounds,
    ListenerUpdate,
    ClearBuffer
};

struct AudioCommand {
    AudioCommandType type;
    int entnum;
    int entchannel;
    sfx_t* sfx;
    Vector3 origin;
    float vol;
    float attenuation;
    bool clear;
    Vector3 v_forward;
    Vector3 v_right;
    Vector3 v_up;
    std::array<int, NUM_AMBIENTS> ambient_vols;
    float host_frametime;
    float ambient_fade;
    bool snd_ambient;
    int random_offset;
};

template <typename T, size_t Capacity>
class SPSCQueue {
public:
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be a power of 2");
    static_assert(std::is_trivially_copyable_v<T>, "Queue objects must be trivially copyable to ensure lock-free safety");

    [[nodiscard]] bool Push(const T& value) {
        size_t write_idx = write_idx_.load(std::memory_order_relaxed);
        size_t read_idx = read_idx_.load(std::memory_order_acquire);
        if (write_idx - read_idx >= Capacity) {
            return false; // Queue full
        }
        buffer_[write_idx & (Capacity - 1)] = value;
        write_idx_.store(write_idx + 1, std::memory_order_release);
        return true;
    }

    [[nodiscard]] bool Pop(T& value) {
        size_t read_idx = read_idx_.load(std::memory_order_relaxed);
        size_t write_idx = write_idx_.load(std::memory_order_acquire);
        if (read_idx == write_idx) {
            return false; // Queue empty
        }
        value = buffer_[read_idx & (Capacity - 1)];
        read_idx_.store(read_idx + 1, std::memory_order_release);
        return true;
    }

private:
    std::array<T, Capacity> buffer_;
    alignas(64) std::atomic<size_t> write_idx_{0};
    alignas(64) std::atomic<size_t> read_idx_{0};
};

SPSCQueue<AudioCommand, 256> command_queue;
float local_volume = 0.7f;
dma_t the_shm;

void S_StartSoundInternal(int entnum, int entchannel, sfx_t* sfx, const Vector3& origin, float fvol, float attenuation, int random_offset);
void S_StaticSoundInternal(sfx_t* sfx, const Vector3& origin, float vol, float attenuation);
void S_StopSoundInternal(int entnum, int entchannel);
void S_StopAllSoundsInternal(bool clear);
void S_ClearBufferInternal(void);
void S_UpdateInternal(const Vector3& origin,
    const Vector3& forward,
    const Vector3& right,
    const Vector3& up,
    float vol_val,
    const std::array<int, NUM_AMBIENTS>& ambient_vols,
    float host_frametime_val,
    float ambient_fade_val,
    bool snd_ambient_val);
void ExecuteAudioCommand(const AudioCommand& cmd);

template <typename T>
[[nodiscard]] constexpr T byteswap(T val) noexcept {
    static_assert(std::is_integral_v<T>, "byteswap requires integral type");
    if constexpr (sizeof(T) == 2) {
        return static_cast<T>((static_cast<unsigned short>(val) << 8) | (static_cast<unsigned short>(val) >> 8));
    } else if constexpr (sizeof(T) == 4) {
        unsigned int u = static_cast<unsigned int>(val);
        return static_cast<T>(
            ((u << 24) & 0xFF000000) |
            ((u << 8)  & 0x00FF0000) |
            ((u >> 8)  & 0x0000FF00) |
            ((u >> 24) & 0x000000FF)
        );
    } else {
        return val;
    }
}

void S_Play(void);
void S_PlayVol(void);
void S_SoundList(void);
void S_Update_();

} // namespace

void S_StopAllSounds(bool clear);

// =======================================================================
// Internal sound data & structures
// =======================================================================

namespace {

std::array<channel_t, MAX_CHANNELS> channels;
std::atomic<int> total_channels{MAX_DYNAMIC_CHANNELS + NUM_AMBIENTS};
bool snd_ambient = true;

Vector3 listener_origin;
Vector3 listener_forward;
Vector3 listener_right;
Vector3 listener_up;

int soundtime;   // sample PAIRS
int paintedtime; // sample PAIRS

bool fakedma = false;
dma_t* shm = nullptr;
bool snd_initialized = false;

} // namespace

int snd_blocked = 0;
vec_t sound_nominal_clip_dist = 1000.0;

namespace {

constexpr int MAX_SFX = 512;
sfx_t* known_sfx = nullptr; // hunk allocated [MAX_SFX]
size_t num_sfx;

std::array<sfx_t*, NUM_AMBIENTS> ambient_sfx;

int desired_speed = 11025;
int desired_bits = 16;

qboolean sound_started = false;

} // namespace

cvar_t bgmvolume = { "bgmvolume", "1", true, {}, {}, {} };
cvar_t volume = { "volume", "0.7", true, {}, {}, {} };

namespace {

cvar_t nosound = { "nosound", "0", {}, {}, {}, {} };
cvar_t precache = { "precache", "1", {}, {}, {}, {} };

} // namespace

cvar_t loadas8bit = { "loadas8bit", "0", {}, {}, {}, {} };

namespace {

cvar_t bgmbuffer = { "bgmbuffer", "4096", {}, {}, {}, {} };
cvar_t ambient_level = { "ambient_level", "0.3", {}, {}, {}, {} };
cvar_t ambient_fade = { "ambient_fade", "100", {}, {}, {}, {} };
cvar_t snd_noextraupdate = { "snd_noextraupdate", "0", {}, {}, {}, {} };
cvar_t snd_show = { "snd_show", "0", {}, {}, {}, {} };
cvar_t _snd_mixahead = { "_snd_mixahead", "0.1", true, {}, {}, {} };

} // namespace

// ====================================================================
// User-setable variables
// ====================================================================



namespace {

void S_SoundInfo_f(void)
{
    if (!sound_started || !shm) {
        Con_Printf("sound system not started\n");

        return;
    }

    Con_Printf("%5d stereo\n", shm->channels.load() - 1);
    Con_Printf("%5d samples\n", shm->samples.load());
    Con_Printf("%5d samplepos\n", shm->samplepos.load());
    Con_Printf("%5d samplebits\n", shm->samplebits.load());
    Con_Printf("%5d submission_chunk\n", shm->submission_chunk.load());
    Con_Printf("%5d speed\n", shm->speed.load());
    Con_Printf("0x%x dma buffer\n", shm->buffer.load());
    Con_Printf("%5d total_channels\n", total_channels.load(std::memory_order_relaxed));
}

} // namespace

/*
================
S_Startup
================
*/

void S_Startup(void)
{
    bool rc;

    if (!snd_initialized) {
        return;
    }

    if (!fakedma) {
        rc = SNDDMA_Init();

        if (!rc) {
            Con_Printf("S_Startup: SNDDMA_Init failed.\n");
            sound_started = 0;

            return;
        }
    }

    sound_started = 1;
}

/*
================
S_Init
================
*/
void S_Init(void)
{
    Con_Printf("\nSound Initialization\n");

    if (COM_CheckParm("-nosound")) {
        return;
    }

    if (COM_CheckParm("-simsound")) {
        fakedma = true;
    }

    Cmd::AddCommand("play", S_Play);
    Cmd::AddCommand("playvol", S_PlayVol);
    Cmd::AddCommand("stopsound", []() { S_StopAllSounds(true); });
    Cmd::AddCommand("soundlist", S_SoundList);
    Cmd::AddCommand("soundinfo", S_SoundInfo_f);

    Cvar::Register(&nosound);
    Cvar::Register(&volume);
    Cvar::Register(&precache);
    Cvar::Register(&loadas8bit);
    Cvar::Register(&bgmvolume);
    Cvar::Register(&bgmbuffer);
    Cvar::Register(&ambient_level);
    Cvar::Register(&ambient_fade);
    Cvar::Register(&snd_noextraupdate);
    Cvar::Register(&snd_show);
    Cvar::Register(&_snd_mixahead);

    if (host_parms.memsize < 0x800000) {
        Cvar::Set("loadas8bit", "1");
        Con_Printf("loading all sounds as 8bit\n");
    }

    snd_initialized = true;

    S_Startup();

    SND_InitScaletable();

    known_sfx = static_cast<sfx_t*>(Hunk_Alloc(MAX_SFX * sizeof(sfx_t), "sfx_t"));
    num_sfx = 0;

    // create a piece of DMA memory

    if (fakedma) {
        shm = &the_shm;
        shm->splitbuffer.store(0, std::memory_order_relaxed);
        shm->samplebits.store(16, std::memory_order_relaxed);
        shm->speed.store(22050, std::memory_order_relaxed);
        shm->channels.store(2, std::memory_order_relaxed);
        shm->samples.store(32768, std::memory_order_relaxed);
        shm->samplepos.store(0, std::memory_order_relaxed);
        shm->soundalive.store(true, std::memory_order_relaxed);
        shm->gamealive.store(true, std::memory_order_relaxed);
        shm->submission_chunk.store(1, std::memory_order_relaxed);
        shm->buffer.store(static_cast<unsigned char*>(Hunk_Alloc(1 << 16, "shmbuf")), std::memory_order_release);
    }

    if (shm) {
        Con_Printf("Sound sampling rate: %i\n", shm->speed.load());
    }

    ambient_sfx[AMBIENT_WATER] = S_PrecacheSound("ambience/water1.wav");
    ambient_sfx[AMBIENT_SKY] = S_PrecacheSound("ambience/wind2.wav");

    S_StopAllSounds(true);
}

// =======================================================================
// Shutdown sound engine
// =======================================================================

void S_Shutdown(void)
{
    if (!sound_started) {
        return;
    }

    if (shm) {
        shm->gamealive.store(0, std::memory_order_release);
    }

    shm = nullptr;
    sound_started = 0;

    if (!fakedma) {
        SNDDMA_Shutdown();
    }
}

// =======================================================================
// Load a sound
// =======================================================================

/*
==================
S_FindName

==================
*/
namespace {

[[nodiscard]] sfx_t* S_FindName(std::string_view name)
{
    if (name.empty()) {
        Sys_Error("S_FindName: NULL\n");
    }

    if (name.length() >= MAX_QPATH) {
        Sys_Error("Sound name too long: %.*s", static_cast<int>(name.length()), name.data());
    }

    // see if already loaded
    for (auto& sfx : std::span(known_sfx, num_sfx)) {
        if (sfx.name == name) {
            return &sfx;
        }
    }

    if (num_sfx == MAX_SFX) {
        Sys_Error("S_FindName: out of sfx_t");
    }

    sfx_t* sfx = &known_sfx[num_sfx];
    name.copy(sfx->name, name.length());
    sfx->name[name.length()] = '\0';

    num_sfx++;

    return sfx;
}

} // namespace

/*
==================
S_TouchSound

==================
*/
void S_TouchSound(std::string_view name)
{
    if (!sound_started) {
        return;
    }

    sfx_t* sfx = S_FindName(name);
    Cache_Check(&sfx->cache);
}

/*
==================
S_PrecacheSound

==================
*/
sfx_t* S_PrecacheSound(std::string_view name)
{
    if (!sound_started || nosound.value) {
        return nullptr;
    }

    sfx_t* sfx = S_FindName(name);

    // cache it in
    if (precache.value) {
        static_cast<void>(S_LoadSound(sfx));
    }

    return sfx;
}

//=============================================================================

/*
=================
SND_PickChannel
=================
*/
channel_t* SND_PickChannel(int entnum, int entchannel)
{
    // Check for replacement sound, or find the best one to replace
    channel_t* first_to_die = nullptr;
    int life_left = std::numeric_limits<int>::max();
    for (auto& chan : std::span(channels).subspan(NUM_AMBIENTS, MAX_DYNAMIC_CHANNELS)) {
        if (entchannel != 0                                                                                            // channel 0 never overrides
            && chan.entnum == entnum && (chan.entchannel == entchannel || entchannel == -1)) { // allways override sound from same entity
            first_to_die = &chan;
            break;
        }

        // don't let monster sounds override player sounds
        if (chan.entnum == cl.viewentity && entnum != cl.viewentity && chan.sfx) {
            continue;
        }

        if (chan.end - paintedtime < life_left) {
            life_left = chan.end - paintedtime;
            first_to_die = &chan;
        }
    }

    if (!first_to_die) {
        return nullptr;
    }

    if (first_to_die->sfx) {
        first_to_die->sfx = nullptr;
    }

    return first_to_die;
}

/*
=================
SND_Spatialize
=================
*/
void SND_Spatialize(channel_t* ch)
{
    vec_t dot;
    vec_t dist;
    vec_t lscale, rscale, scale;
    Vector3 source_vec;

    // anything coming from the view entity will allways be full volume
    if (ch->entnum == cl.viewentity) {
        ch->leftvol = ch->master_vol;
        ch->rightvol = ch->master_vol;

        return;
    }

    // calculate stereo seperation and distance attenuation

    source_vec = ch->origin - listener_origin;

    dist = source_vec.normalize() * ch->dist_mult;

    dot = listener_right.dot(source_vec);

    if (shm->channels.load(std::memory_order_relaxed) == 1) {
        rscale = 1.0;
        lscale = 1.0;
    } else {
        rscale = static_cast<vec_t>(1.0 + dot);
        lscale = static_cast<vec_t>(1.0 - dot);
    }

    // add in distance effect
    scale = static_cast<vec_t>((1.0 - dist) * rscale);
    ch->rightvol = static_cast<int>(ch->master_vol * scale);
    if (ch->rightvol < 0) {
        ch->rightvol = 0;
    }

    scale = static_cast<vec_t>((1.0 - dist) * lscale);
    ch->leftvol = static_cast<int>(ch->master_vol * scale);
    if (ch->leftvol < 0) {
        ch->leftvol = 0;
    }
}

// =======================================================================
// Start a sound effect
// =======================================================================



namespace {

void S_StartSoundInternal(int entnum,
    int entchannel,
    sfx_t* sfx,
    const Vector3& origin,
    float fvol,
    float attenuation,
    int random_offset)
{
    channel_t *target_chan;
    sfxcache_t* sc;
    int vol;

    vol = static_cast<int>(fvol * 255);

    // pick a channel to play on
    target_chan = SND_PickChannel(entnum, entchannel);
    if (!target_chan) {
        return;
    }

    // spatialize
    *target_chan = {};
    target_chan->origin = origin;
    target_chan->dist_mult = attenuation / sound_nominal_clip_dist;
    target_chan->master_vol = vol;
    target_chan->entnum = entnum;
    target_chan->entchannel = entchannel;
    SND_Spatialize(target_chan);

    if (!target_chan->leftvol && !target_chan->rightvol) {
        return; // not audible at all
    }

    // new channel (strictly check cache only, no I/O or heap allocation on audio thread)
    sc = static_cast<sfxcache_t*>(Cache_Check(&sfx->cache));
    if (!sc) {
        target_chan->sfx = nullptr;
        return;
    }

    target_chan->sfx = sfx;
    target_chan->pos = static_cast<int>(0.0);
    target_chan->end = paintedtime + sc->length;

    // if an identical sound has also been started this frame, offset the pos
    // a bit to keep it from just making the first one louder
    for (auto& check : std::span(channels).subspan(NUM_AMBIENTS, MAX_DYNAMIC_CHANNELS)) {
        if (&check == target_chan) {
            continue;
        }

        if (check.sfx == sfx && !check.pos) {
            int skip = random_offset;
            if (skip >= target_chan->end) {
                skip = target_chan->end - 1;
            }
            if (skip < 0) {
                skip = 0;
            }

            target_chan->pos += skip;
            target_chan->end -= skip;
            break;
        }
    }
}

void S_StopSoundInternal(int entnum, int entchannel)
{
    for (auto& chan : std::span(channels).first(MAX_DYNAMIC_CHANNELS)) {
        if (chan.entnum == entnum && chan.entchannel == entchannel) {
            chan.end = 0;
            chan.sfx = nullptr;

            return;
        }
    }
}

void S_StopAllSoundsInternal(bool clear)
{
    total_channels = MAX_DYNAMIC_CHANNELS + NUM_AMBIENTS; // no statics

    for (auto& chan : channels) {
        if (chan.sfx) {
            chan.sfx = nullptr;
        }
    }

    channels.fill({});

    if (clear) {
        S_ClearBufferInternal();
    }
}

void S_ClearBufferInternal(void)
{
}

void S_StaticSoundInternal(sfx_t* sfx, const Vector3& origin, float vol, float attenuation)
{
    channel_t* ss;
    sfxcache_t* sc;

    if (total_channels == MAX_CHANNELS) {
        return;
    }

    ss = &channels[total_channels];
    total_channels++;

    // strictly check cache only
    sc = static_cast<sfxcache_t*>(Cache_Check(&sfx->cache));
    if (!sc) {
        total_channels--; // Revert increment since sound could not be loaded/found in cache
        return;
    }

    if (sc->loopstart == -1) {
        return;
    }

    ss->sfx = sfx;
    ss->origin = origin;
    ss->master_vol = static_cast<int>(vol);
    ss->dist_mult = (attenuation / 64) / sound_nominal_clip_dist;
    ss->end = paintedtime + sc->length;

    SND_Spatialize(ss);
}

void ExecuteAudioCommand(const AudioCommand& cmd)
{
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
        S_UpdateInternal(cmd.origin, cmd.v_forward, cmd.v_right, cmd.v_up, cmd.vol, cmd.ambient_vols, cmd.host_frametime, cmd.ambient_fade, cmd.snd_ambient);
        break;
    case AudioCommandType::ClearBuffer:
        S_ClearBufferInternal();
        break;
    }
}

} // namespace

void S_StartSound(int entnum,
    int entchannel,
    sfx_t* sfx,
    const Vector3& origin,
    float fvol,
    float attenuation)
{
    if (!sound_started || !sfx || nosound.value) {
        return;
    }

    // strictly check cache only during active gameplay; drop sound if not precached
    if (!Cache_Check(&sfx->cache)) {
        return;
    }

    AudioCommand cmd{};
    cmd.type = AudioCommandType::StartSound;
    cmd.entnum = entnum;
    cmd.entchannel = entchannel;
    cmd.sfx = sfx;
    cmd.origin = origin;
    cmd.vol = fvol;
    cmd.attenuation = attenuation;

    // Calculate random offset on main thread (thread-safe, no system calls on audio thread)
    int random_offset = 0;
    if (shm) {
        int max_skip = static_cast<int>(0.1 * shm->speed.load(std::memory_order_relaxed));
        if (max_skip > 0) {
            thread_local std::mt19937 generator(std::random_device{}());
            std::uniform_int_distribution<int> distribution(0, max_skip - 1);
            random_offset = distribution(generator);
        }
    }
    cmd.random_offset = random_offset;

    if (!command_queue.Push(cmd)) {
        Con_Printf("WARNING: Audio command queue overflow!\n");
    }
}

void S_StaticSound(sfx_t* sfx, const Vector3& origin, float vol, float attenuation)
{
    if (!sound_started || !sfx) {
        return;
    }

    // strictly check cache only during active gameplay; drop sound if not precached
    if (!Cache_Check(&sfx->cache)) {
        return;
    }

    AudioCommand cmd{};
    cmd.type = AudioCommandType::StaticSound;
    cmd.sfx = sfx;
    cmd.origin = origin;
    cmd.vol = vol;
    cmd.attenuation = attenuation;

    if (!command_queue.Push(cmd)) {
        Con_Printf("WARNING: Audio command queue overflow!\n");
    }
}

void S_StopSound(int entnum, int entchannel)
{
    if (!sound_started) {
        return;
    }

    AudioCommand cmd{};
    cmd.type = AudioCommandType::StopSound;
    cmd.entnum = entnum;
    cmd.entchannel = entchannel;

    if (!command_queue.Push(cmd)) {
        Con_Printf("WARNING: Audio command queue overflow!\n");
    }
}

void S_StopAllSounds(bool clear)
{
    if (!sound_started) {
        return;
    }

    AudioCommand cmd{};
    cmd.type = AudioCommandType::StopAllSounds;
    cmd.clear = clear;

    if (!command_queue.Push(cmd)) {
        Con_Printf("WARNING: Audio command queue overflow!\n");
    }
}

void S_ClearBuffer(void)
{
    if (!sound_started) {
        return;
    }

    AudioCommand cmd{};
    cmd.type = AudioCommandType::ClearBuffer;

    if (!command_queue.Push(cmd)) {
        Con_Printf("WARNING: Audio command queue overflow!\n");
    }
}

//=============================================================================

/*
===================
S_UpdateAmbientSounds
===================
*/
namespace {

void S_UpdateInternal(const Vector3& origin,
    const Vector3& forward,
    const Vector3& right,
    const Vector3& up,
    float vol_val,
    const std::array<int, NUM_AMBIENTS>& ambient_vols,
    float host_frametime_val,
    float ambient_fade_val,
    bool snd_ambient_val)
{
    int total;
    channel_t* combine;

    listener_origin = origin;
    listener_forward = forward;
    listener_right = right;
    listener_up = up;
    local_volume = vol_val;

    // update general area ambient sound sources (thread-safe volume interpolation on the audio thread)
    if (!snd_ambient_val) {
        for (auto& ambient_chan : std::span(channels).first(NUM_AMBIENTS)) {
            ambient_chan.sfx = nullptr;
        }
    } else {
        for (int ambient_channel = 0; ambient_channel < NUM_AMBIENTS; ambient_channel++) {
            channel_t* chan = &channels[ambient_channel];
            chan->sfx = ambient_sfx[ambient_channel];

            int target_vol = ambient_vols[ambient_channel];

            // don't adjust volume too fast
            if (chan->master_vol < target_vol) {
                chan->master_vol = static_cast<int>(chan->master_vol + host_frametime_val * ambient_fade_val);
                if (chan->master_vol > target_vol) {
                    chan->master_vol = target_vol;
                }
            } else if (chan->master_vol > target_vol) {
                chan->master_vol = static_cast<int>(chan->master_vol - host_frametime_val * ambient_fade_val);
                if (chan->master_vol < target_vol) {
                    chan->master_vol = target_vol;
                }
            }

            chan->leftvol = chan->rightvol = chan->master_vol;
        }
    }

    combine = nullptr;

    // update spatialization for static and dynamic sounds
    int i = NUM_AMBIENTS;
    for (auto& ch : std::span(channels).subspan(NUM_AMBIENTS, total_channels - NUM_AMBIENTS)) {
        if (!ch.sfx) {
            i++;
            continue;
        }

        SND_Spatialize(&ch); // respatialize channel
        if (!ch.leftvol && !ch.rightvol) {
            i++;
            continue;
        }

        // try to combine static sounds with a previous channel of the same
        // sound effect so we don't mix five torches every frame

        if (i >= MAX_DYNAMIC_CHANNELS + NUM_AMBIENTS) {
            // see if it can just use the last one
            if (combine && combine->sfx == ch.sfx) {
                combine->leftvol += ch.leftvol;
                combine->rightvol += ch.rightvol;
                ch.leftvol = ch.rightvol = 0;
                i++;
                continue;
            }

            // search for one
            combine = &channels[MAX_DYNAMIC_CHANNELS + NUM_AMBIENTS];
            int j;
            for (j = MAX_DYNAMIC_CHANNELS + NUM_AMBIENTS; j < i; j++, combine++) {
                if (combine->sfx == ch.sfx) {
                    break;
                }
            }

            if (j == total_channels) {
                combine = nullptr;
            } else {
                if (combine != &ch) {
                    combine->leftvol += ch.leftvol;
                    combine->rightvol += ch.rightvol;
                    ch.leftvol = ch.rightvol = 0;
                }

                i++;
                continue;
            }
        }
        i++;
    }

    // debugging output (only printed if fakedma is active to preserve real-time safety of background audio thread)
    if (fakedma && snd_show.value) {
        total = 0;
        for (auto& ch : std::span(channels).first(total_channels)) {
            if (ch.sfx && (ch.leftvol || ch.rightvol)) {
                total++;
            }
        }

        Con_Printf("----(%i)----\n", total);
    }
}

} // namespace

void S_Update(const Vector3& origin, const Vector3& forward, const Vector3& right, const Vector3& up)
{
    if (!sound_started || (snd_blocked > 0)) {
        return;
    }

    AudioCommand cmd{};
    cmd.type = AudioCommandType::ListenerUpdate;
    cmd.origin = origin;
    cmd.v_forward = forward;
    cmd.v_right = right;
    cmd.v_up = up;
    cmd.vol = volume.value;

    // Calculate ambient sound levels on the main thread (thread-safe worldmodel leaf traversal)
    cmd.snd_ambient = snd_ambient;
    cmd.ambient_fade = ambient_fade.value;
    cmd.host_frametime = static_cast<float>(host_frametime);
    cmd.ambient_vols.fill(0);

    if (snd_ambient && cl.worldmodel && ambient_level.value) {
        mleaf_t* l = Mod_PointInLeaf(origin, cl.worldmodel);
        if (l) {
            for (int ambient_channel = 0; ambient_channel < NUM_AMBIENTS; ambient_channel++) {
                float vol = ambient_level.value * l->ambient_sound_level[ambient_channel];
                if (vol < 8) {
                    vol = 0;
                }
                cmd.ambient_vols[ambient_channel] = static_cast<int>(vol);
            }
        }
    }

    if (!command_queue.Push(cmd)) {
        Con_Printf("WARNING: Audio command queue overflow!\n");
    }

    if (fakedma) {
        AudioCommand c{};
        while (command_queue.Pop(c)) {
            ExecuteAudioCommand(c);
        }
    }
}

void S_ExtraUpdate(void)
{

    if (snd_noextraupdate.value) {
        return; // don't pollute timings
    }

    S_Update_();
}

namespace {

void S_Update_(void)
{
}

/*
===============================================================================

console functions

===============================================================================
*/

void S_Play(void)
{
    thread_local std::mt19937 rng(std::random_device{}());
    std::uniform_int_distribution<int> dist(0, 1000);
    int hash = dist(rng);
    sfx_t* sfx;

    int i = 1;
    while (i < Cmd::Argc()) {
        std::string name;
        auto arg = Cmd::Argv(i);
        if (arg.find('.') == std::string_view::npos) {
            name = std::string(arg) + ".wav";
        } else {
            name = std::string(arg);
        }

        sfx = S_PrecacheSound(name);
        Vector3 play_origin = cl_entities[cl.viewentity].origin;
        S_StartSound(hash++, 0, sfx, play_origin, 1.0, 1.0);
        i++;
    }
}

void S_PlayVol(void)
{
    thread_local std::mt19937 rng(std::random_device{}());
    std::uniform_int_distribution<int> dist(0, 1000);
    int hash = dist(rng);
    float vol;
    sfx_t* sfx;

    int i = 1;
    while (i < Cmd::Argc()) {
        std::string name;
        auto arg = Cmd::Argv(i);
        if (arg.find('.') == std::string_view::npos) {
            name = std::string(arg) + ".wav";
        } else {
            name = std::string(arg);
        }

        sfx = S_PrecacheSound(name);
        auto arg_vol = Cmd::Argv(i + 1);
        vol = 1.0f;
        std::from_chars(arg_vol.data(), arg_vol.data() + arg_vol.size(), vol);
        Vector3 play_origin = cl_entities[cl.viewentity].origin;
        S_StartSound(hash++, 0, sfx, play_origin, vol, 1.0);
        i += 2;
    }
}

void S_SoundList(void)
{
    sfxcache_t* sc;
    int size, total;

    total = 0;
    for (auto& sfx : std::span(known_sfx, num_sfx)) {
        sc = static_cast<sfxcache_t*>(Cache_Check(&sfx.cache));
        if (!sc) {
            continue;
        }

        size = sc->length * sc->width * (sc->stereo + 1);
        total += size;
        if (sc->loopstart >= 0) {
            Con_Printf("L");
        } else {
            Con_Printf(" ");
        }

        Con_Printf("%s\n", sfx.name);
    }
    Con_Printf("Total sound memory: %i\n", total);
}

} // namespace

void S_LocalSound(std::string_view sound)
{
    if (nosound.value || !sound_started) {
        return;
    }

    sfx_t* sfx = S_FindName(sound);
    if (!sfx || !Cache_Check(&sfx->cache)) {
        Con_Printf("WARNING: S_LocalSound attempted to play non-precached sound: %.*s\n", 
                   static_cast<int>(sound.length()), sound.data());
        return;
    }

    S_StartSound(cl.viewentity, -1, sfx, vec3_origin, 1.0f, 1.0f);
}

void S_BeginPrecaching(void)
{
}

void S_EndPrecaching(void)
{
}

// ============================================================================
// snd_mem.cpp -- sound caching and WAV loading
// ============================================================================

int cache_full_cycle;

byte* S_Alloc(int size);

/*
================
ResampleSfx
================
*/
namespace {

void ResampleSfx(sfx_t* sfx, int inrate, int inwidth, byte* data)
{
    int outcount;
    int srcsample;
    float stepscale;
    int sample, samplefrac, fracstep;
    sfxcache_t* sc;

    sc = static_cast<sfxcache_t*>(Cache_Check(&sfx->cache));
    if (!sc) {
        return;
    }

    stepscale = static_cast<float>(inrate) / shm->speed.load(); // this is usually 0.5, 1, or 2

    outcount = static_cast<int>(sc->length / stepscale);
    sc->length = outcount;
    if (sc->loopstart != -1) {
        sc->loopstart = static_cast<int>(sc->loopstart / stepscale);
    }

    sc->speed = shm->speed.load();
    if (loadas8bit.value) {
        sc->width = 1;
    } else {
        sc->width = inwidth;
    }

    sc->stereo = 0;

    // resample / decimate to the current source rate

    if (stepscale == 1 && inwidth == 1 && sc->width == 1) {
        // fast special case
        for (int i = 0; i < outcount; i++) {
            reinterpret_cast<signed char*>(sc->data)[i] = static_cast<int>(data[i] - 128);
        }
    } else {
        // general case
        samplefrac = 0;
        fracstep = static_cast<int>(stepscale * 256);
        for (int i = 0; i < outcount; i++) {
            srcsample = samplefrac >> 8;
            samplefrac += fracstep;
            if (inwidth == 2) {
                short val;
                std::memcpy(&val, &data[srcsample * 2], sizeof(short));
                if constexpr (std::endian::native == std::endian::big) {
                    val = byteswap(val);
                }
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

} // namespace

//=============================================================================

/*
==============
S_LoadSound
==============
*/
sfxcache_t* S_LoadSound(sfx_t* s)
{
    byte* data;
    wavinfo_t info;
    int len;
    float stepscale;
    sfxcache_t* sc;
    std::array<byte, 1024> stackbuf; // avoid dirtying the cache heap

    // see if still in memory
    sc = static_cast<sfxcache_t*>(Cache_Check(&s->cache));
    if (sc) {
        return sc;
    }

    // load it in using stack allocation for path construction (no heap alloc)
    std::array<char, MAX_QPATH + 16> namebuffer;
    std::snprintf(namebuffer.data(), namebuffer.size(), "sound/%s", s->name);

    data = COM_LoadStackFile(namebuffer.data(), stackbuf.data(), sizeof(stackbuf));

    if (!data) {
        Con_Printf("Couldn't load %s\n", namebuffer.data());

        return nullptr;
    }

    // Call GetWavinfo using a std::span for strict bounds checking
    info = GetWavinfo(s->name, std::span<const byte>(data, com_filesize));
    if (info.channels != 1) {
        Con_Printf("%s is a stereo sample\n", s->name);

        return nullptr;
    }

    stepscale = static_cast<float>(info.rate) / shm->speed.load(std::memory_order_relaxed);
    len = static_cast<int>(info.samples / stepscale);

    len = len * info.width * info.channels;

    sc = static_cast<sfxcache_t*>(Cache_Alloc(&s->cache, len + sizeof(sfxcache_t), s->name));
    if (!sc) {
        return nullptr;
    }

    sc->length = info.samples;
    sc->loopstart = info.loopstart;
    sc->speed = info.rate;
    sc->width = info.width;
    sc->stereo = info.channels;

    ResampleSfx(s, sc->speed, sc->width, data + info.dataofs);

    return sc;
}

/*
===============================================================================

WAV loading

===============================================================================
*/

namespace {

class WavParser {
public:
    explicit WavParser(std::span<const byte> wav_data)
        : file_data_(wav_data), iff_data_offset_(0), last_chunk_offset_(0), current_chunk_offset_(wav_data.size()), iff_chunk_len_(0) {}

    [[nodiscard]] bool FindNextChunk(std::string_view name) {
        while (true) {
            size_t offset = last_chunk_offset_;
            if (offset + 8 > file_data_.size()) {
                current_chunk_offset_ = file_data_.size();
                return false;
            }

            int len = static_cast<int>(file_data_[offset + 4] |
                                       (file_data_[offset + 5] << 8) |
                                       (file_data_[offset + 6] << 16) |
                                       (file_data_[offset + 7] << 24));
            if (len < 0) {
                current_chunk_offset_ = file_data_.size();
                return false;
            }

            iff_chunk_len_ = static_cast<size_t>(len);
            current_chunk_offset_ = offset;
            last_chunk_offset_ = offset + 8 + ((iff_chunk_len_ + 1) & ~1);

            if (std::string_view(reinterpret_cast<const char*>(&file_data_[offset]), 4) == name) {
                return true;
            }
        }
    }

    void FindChunk(std::string_view name) {
        last_chunk_offset_ = iff_data_offset_;
        static_cast<void>(FindNextChunk(name));
    }

    [[nodiscard]] short ReadShort(size_t& offset) const {
        if (offset + 2 > file_data_.size()) {
            return 0;
        }
        short val = static_cast<short>(file_data_[offset] | (file_data_[offset + 1] << 8));
        offset += 2;
        return val;
    }

    [[nodiscard]] int ReadLong(size_t& offset) const {
        if (offset + 4 > file_data_.size()) {
            return 0;
        }
        int val = static_cast<int>(file_data_[offset] | 
                                   (file_data_[offset + 1] << 8) | 
                                   (file_data_[offset + 2] << 16) | 
                                   (file_data_[offset + 3] << 24));
        offset += 4;
        return val;
    }

    [[nodiscard]] bool HasFoundChunk() const {
        return current_chunk_offset_ < file_data_.size();
    }

    [[nodiscard]] size_t GetCurrentChunkPayloadOffset() const {
        return current_chunk_offset_ + 8;
    }

    [[nodiscard]] size_t GetCurrentChunkLength() const {
        return iff_chunk_len_;
    }

    void SetIffDataOffset(size_t offset) {
        iff_data_offset_ = offset;
    }

private:
    std::span<const byte> file_data_;
    size_t iff_data_offset_;
    size_t last_chunk_offset_;
    size_t current_chunk_offset_;
    size_t iff_chunk_len_;
};

} // namespace

/*
============
GetWavinfo
============
*/
wavinfo_t GetWavinfo(std::string_view name, std::span<const byte> wav_data)
{
    wavinfo_t info{};

    if (wav_data.empty()) {
        return info;
    }

    WavParser parser(wav_data);

    // find "RIFF" chunk
    parser.FindChunk("RIFF");
    if (!parser.HasFoundChunk()) {
        Con_Printf("Missing RIFF chunk\n");

        return info;
    }

    size_t riff_payload_offset = parser.GetCurrentChunkPayloadOffset();
    if (riff_payload_offset + 4 > wav_data.size()) {
        Con_Printf("Malformed RIFF chunk\n");

        return info;
    }

    if (std::string_view(reinterpret_cast<const char*>(&wav_data[riff_payload_offset]), 4) != "WAVE") {
        Con_Printf("Missing WAVE format inside RIFF\n");

        return info;
    }

    // fmt chunk searches inside the RIFF chunk. RIFF subchunks start at payload + 4 (after "WAVE")
    parser.SetIffDataOffset(riff_payload_offset + 4);

    parser.FindChunk("fmt ");
    if (!parser.HasFoundChunk()) {
        Con_Printf("Missing fmt chunk\n");

        return info;
    }

    size_t fmt_offset = parser.GetCurrentChunkPayloadOffset();
    int format = parser.ReadShort(fmt_offset);
    if (format != 1) {
        Con_Printf("Microsoft PCM format only\n");

        return info;
    }

    info.channels = parser.ReadShort(fmt_offset);
    info.rate = parser.ReadLong(fmt_offset);
    
    // Skip 4 + 2 bytes: dwAvgBytesPerSec (4) and wBlockAlign (2)
    fmt_offset += 4 + 2;
    
    info.width = parser.ReadShort(fmt_offset) / 8;

    // get cue chunk
    parser.FindChunk("cue ");
    if (parser.HasFoundChunk()) {
        size_t cue_offset = parser.GetCurrentChunkPayloadOffset();
        cue_offset += 32; // Skip to loopstart
        info.loopstart = parser.ReadLong(cue_offset);

        // if the next chunk is a LIST chunk, look for a cue length marker
        if (parser.FindNextChunk("LIST")) {
            size_t list_offset = parser.GetCurrentChunkPayloadOffset();
            if (list_offset + 32 <= wav_data.size()) {
                if (std::string_view(reinterpret_cast<const char*>(&wav_data[list_offset + 28]), 4) == "mark") {
                    list_offset += 24;
                    int i = parser.ReadLong(list_offset); // samples in loop
                    info.samples = info.loopstart + i;
                }
            }
        }
    } else {
        info.loopstart = -1;
    }

    // find data chunk
    parser.FindChunk("data");
    if (!parser.HasFoundChunk()) {
        Con_Printf("Missing data chunk\n");

        return info;
    }

    size_t data_offset = parser.GetCurrentChunkPayloadOffset();
    int samples = static_cast<int>(parser.GetCurrentChunkLength()) / info.width;

    if (info.samples) {
        if (samples < info.samples) {
            Sys_Error("Sound %.*s has a bad loop length", static_cast<int>(name.length()), name.data());
        }
    } else {
        info.samples = samples;
    }

    info.dataofs = static_cast<int>(data_offset);

    return info;
}

// ============================================================================
// snd_mix.cpp -- portable code to mix sounds
// ============================================================================

#include <cstdint>

namespace {

constexpr int PAINTBUFFER_SIZE = 512;
std::array<portable_samplepair_t, PAINTBUFFER_SIZE> paintbuffer;
std::array<std::array<int, 256>, 32> snd_scaletable;

void Snd_WriteLinearBlastStereo16(const int* snd_p, short* snd_out, int snd_linear_count, int snd_vol)
{
    int val;

    for (int i = 0; i < snd_linear_count; i += 2) {
        val = (snd_p[i] * snd_vol) >> 8;
        if (val > std::numeric_limits<short>::max()) {
            snd_out[i] = std::numeric_limits<short>::max();
        } else if (val < std::numeric_limits<short>::min()) {
            snd_out[i] = std::numeric_limits<short>::min();
        } else {
            snd_out[i] = static_cast<short>(val);
        }

        val = (snd_p[i + 1] * snd_vol) >> 8;
        if (val > std::numeric_limits<short>::max()) {
            snd_out[i + 1] = std::numeric_limits<short>::max();
        } else if (val < std::numeric_limits<short>::min()) {
            snd_out[i + 1] = std::numeric_limits<short>::min();
        } else {
            snd_out[i + 1] = static_cast<short>(val);
        }
    }
}

void S_TransferStereo16(int endtime)
{
    int lpaintedtime = paintedtime;

    auto snd_p = reinterpret_cast<const int*>(paintbuffer.data());
    int snd_vol = static_cast<int>(local_volume * 256);

    while (lpaintedtime < endtime) {
        int lpos = lpaintedtime & ((shm->samples.load() >> 1) - 1);

        auto snd_out = reinterpret_cast<short*>(shm->buffer.load()) + (lpos << 1);

        int snd_linear_count = (shm->samples.load() >> 1) - lpos;
        if (lpaintedtime + snd_linear_count > endtime) {
            snd_linear_count = endtime - lpaintedtime;
        }

        snd_linear_count <<= 1;

        // write a linear blast of samples
        Snd_WriteLinearBlastStereo16(snd_p, snd_out, snd_linear_count, snd_vol);

        snd_p += snd_linear_count;
        lpaintedtime += (snd_linear_count >> 1);
    }
}

void S_TransferPaintBuffer(int endtime)
{
    int out_idx;
    int count;
    int out_mask;
    int* p;
    int step;
    int val;
    int mix_vol;
    unsigned char* pbuf;

    int samplebits_val = shm->samplebits.load(std::memory_order_relaxed);
    int channels_val = shm->channels.load(std::memory_order_relaxed);

    if (samplebits_val == 16 && channels_val == 2) {
        S_TransferStereo16(endtime);

        return;
    }

    p = reinterpret_cast<int*>(paintbuffer.data());
    count = (endtime - paintedtime) * channels_val;
    out_mask = shm->samples.load(std::memory_order_relaxed) - 1;
    out_idx = paintedtime * channels_val & out_mask;
    step = 3 - channels_val;
    mix_vol = static_cast<int>(local_volume * 256);

    {
        pbuf = static_cast<unsigned char*>(shm->buffer.load());
    }

    if (samplebits_val == 16) {
        short* out = reinterpret_cast<short*>(pbuf);
        while (count--) {
            val = (*p * mix_vol) >> 8;
            p += step;
            if (val > std::numeric_limits<short>::max()) {
                val = std::numeric_limits<short>::max();
            } else if (val < std::numeric_limits<short>::min()) {
                val = std::numeric_limits<short>::min();
            }

            out[out_idx] = static_cast<short>(val);
            out_idx = (out_idx + 1) & out_mask;
        }
    } else if (samplebits_val == 8) {
        unsigned char* out = static_cast<unsigned char*>(pbuf);
        while (count--) {
            val = (*p * mix_vol) >> 8;
            p += step;
            if (val > std::numeric_limits<short>::max()) {
                val = std::numeric_limits<short>::max();
            } else if (val < std::numeric_limits<short>::min()) {
                val = std::numeric_limits<short>::min();
            }

            out[out_idx] = static_cast<unsigned char>((val >> 8) + 128);
            out_idx = (out_idx + 1) & out_mask;
        }
    }
}

} // namespace

/*
===============================================================================

CHANNEL MIXING

===============================================================================
*/

namespace {

void SND_PaintChannelFrom8(channel_t* ch, sfxcache_t* sc, int count, int offset);
void SND_PaintChannelFrom16(channel_t* ch, sfxcache_t* sc, int count, int offset);

} // namespace

void S_PaintChannels(int endtime)
{
    int end;
    sfxcache_t* sc;
    int ltime, count;

    while (paintedtime < endtime) {
        // if paintbuffer is smaller than DMA buffer
        end = endtime;
        if (endtime - paintedtime > PAINTBUFFER_SIZE) {
            end = paintedtime + PAINTBUFFER_SIZE;
        }

        // clear the paint buffer
        std::fill_n(paintbuffer.begin(), end - paintedtime, portable_samplepair_t{0, 0});

        // paint in the channels.
        for (int i = 0; i < total_channels; i++) {
            auto& chan = channels[i];
            if (!chan.sfx) {
                continue;
            }

            if (!chan.leftvol && !chan.rightvol) {
                continue;
            }

            sc = static_cast<sfxcache_t*>(Cache_Check(&chan.sfx->cache));
            if (!sc) {
                chan.sfx = nullptr;
                continue;
            }

            ltime = paintedtime;

            while (ltime < end) { // paint up to end
                if (chan.end < end) {
                    count = chan.end - ltime;
                } else {
                    count = end - ltime;
                }

                if (count > 0) {
                    if (sc->width == 1) {
                        SND_PaintChannelFrom8(&chan, sc, count, ltime - paintedtime);
                    } else {
                        SND_PaintChannelFrom16(&chan, sc, count, ltime - paintedtime);
                    }

                    ltime += count;
                }

                // if at end of loop, restart
                if (ltime >= chan.end) {
                    if (sc->loopstart >= 0) {
                        chan.pos = sc->loopstart;
                        chan.end = ltime + sc->length - chan.pos;
                    } else { // channel just stopped
                        chan.sfx = nullptr;
                        break;
                    }
                }
            }
        }

        // transfer out according to DMA format
        S_TransferPaintBuffer(end);
        paintedtime = end;
    }
}

void SND_InitScaletable(void)
{
    for (int i = 0; i < 32; i++) {
        for (int j = 0; j < 256; j++) {
            snd_scaletable[i][j] = static_cast<signed char>(j) * i * 8;
        }
    }
}

namespace {

void SND_PaintChannelFrom8(channel_t* ch, sfxcache_t* sc, int count, int offset)
{
    int data;
    int *lscale, *rscale;
    unsigned char* sfx;

    if (ch->leftvol > 255) {
        ch->leftvol = 255;
    }

    if (ch->rightvol > 255) {
        ch->rightvol = 255;
    }

    lscale = snd_scaletable[ch->leftvol >> 3].data();
    rscale = snd_scaletable[ch->rightvol >> 3].data();
    sfx = static_cast<unsigned char*>(sc->data) + ch->pos;

    for (int i = 0; i < count; i++) {
        data = sfx[i];
        paintbuffer[offset + i].left += lscale[data];
        paintbuffer[offset + i].right += rscale[data];
    }

    ch->pos += count;
}

void SND_PaintChannelFrom16(channel_t* ch, sfxcache_t* sc, int count, int offset)
{
    int leftvol = ch->leftvol;
    int rightvol = ch->rightvol;

    for (int i = 0; i < count; i++) {
        short data_val;
        // safely extract the 16-bit sample
        std::memcpy(&data_val, sc->data + ((ch->pos + i) * 2), sizeof(short));

        int left = (data_val * leftvol) >> 8;
        int right = (data_val * rightvol) >> 8;

        paintbuffer[offset + i].left += left;
        paintbuffer[offset + i].right += right;
    }

    ch->pos += count;
}

} // namespace

// ============================================================================
// snd_sdl.cpp -- SDL audio output driver
// ============================================================================

namespace {

int snd_inited;

void paint_audio(void* /*unused*/, Uint8* stream, int len)
{
    if (shm) {
        AudioCommand cmd{};
        while (command_queue.Pop(cmd)) {
            ExecuteAudioCommand(cmd);
        }

        shm->buffer.store(stream, std::memory_order_release);
        int samplebits_val = shm->samplebits.load(std::memory_order_relaxed);
        int current_pos = shm->samplepos.load(std::memory_order_acquire);
        int next_pos = current_pos + len / (samplebits_val / 8) / 2;
        shm->samplepos.store(next_pos, std::memory_order_release);

        S_PaintChannels(next_pos);
    }
}

} // namespace

bool SNDDMA_Init(void)
{
    SDL_AudioSpec desired;

    snd_inited = 0;

    /* Set up the desired format */
    desired.freq = desired_speed;
    switch (desired_bits) {
    case 8:
        desired.format = AUDIO_U8;
        break;
    case 16:
        if constexpr (SDL_BYTEORDER == SDL_BIG_ENDIAN) {
            desired.format = AUDIO_S16MSB;
        } else {
            desired.format = AUDIO_S16LSB;
        }

        break;
    default:
        Con_Printf("Unknown number of audio bits: %d\n", desired_bits);

        return false;
    }
    desired.channels = 2;
    desired.samples = 512;
    static_assert((512 & (512 - 1)) == 0, "SDL desired samples must be a power of two for bitwise modulo to work");
    desired.callback = paint_audio;

    /* Open the audio device */
    if (SDL_OpenAudio(&desired, nullptr) < 0) {
        Con_Printf("Couldn't open SDL audio: %s\n", SDL_GetError());

        return false;
    }
    SDL_PauseAudio(0);

    // Ensure samples count is a power of two to support fast bitwise modulo operations
    int negotiated_samples = desired.samples * desired.channels;
    if ((negotiated_samples & (negotiated_samples - 1)) != 0) {
        Sys_Error("SNDDMA_Init: Negotiated buffer size (%d) is not a power of two", negotiated_samples);
    }

    /* Fill the audio DMA information block */
    shm = &the_shm;
    shm->splitbuffer.store(0, std::memory_order_relaxed);
    shm->samplebits.store(static_cast<int>(desired.format & 0xFF), std::memory_order_relaxed);
    shm->speed.store(desired.freq, std::memory_order_relaxed);
    shm->channels.store(desired.channels, std::memory_order_relaxed);
    shm->samples.store(desired.samples * desired.channels, std::memory_order_relaxed);
    shm->samplepos.store(0, std::memory_order_relaxed);
    shm->submission_chunk.store(1, std::memory_order_relaxed);
    shm->buffer.store(nullptr, std::memory_order_release);

    snd_inited = 1;

    return true;
}

void SNDDMA_Shutdown(void)
{
    if (snd_inited) {
        SDL_CloseAudio();
        snd_inited = 0;
    }
}

} // namespace Audio
