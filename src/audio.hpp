// audio.hpp -- client sound i/o functions
#pragma once

#include <array>
#include <string_view>
#include <atomic>
#include <span>

namespace Audio {

inline constexpr int DEFAULT_SOUND_PACKET_VOLUME = 255;
inline constexpr float DEFAULT_SOUND_PACKET_ATTENUATION = 1.0f;

// !!! if this is changed, it much be changed in asm_i386.h too !!!
struct portable_samplepair_t {
    int left;
    int right;
};

struct sfx_s {
    char name[MAX_QPATH];
    cache_user_t cache;
};

using sfx_t = sfx_s;

// !!! if this is changed, it much be changed in asm_i386.h too !!!
#pragma warning(push)
#pragma warning(disable: 4200) // Silence nonstandard extension: zero-sized array warning
struct sfxcache_t {
    int length;
    int loopstart;
    int speed;
    int width;
    int stereo;
    byte data[]; // variable sized
};
#pragma warning(pop)

struct dma_t {
    std::atomic<bool> gamealive;
    std::atomic<bool> soundalive;
    std::atomic<bool> splitbuffer;
    std::atomic<int> channels;
    std::atomic<int> samples;          // mono samples in buffer
    std::atomic<int> submission_chunk; // don't mix less than this #
    std::atomic<int> samplepos;        // in mono samples
    std::atomic<int> samplebits;
    std::atomic<int> speed;
    std::atomic<unsigned char*> buffer;
};

// !!! if this is changed, it much be changed in asm_i386.h too !!!
struct channel_t {
    sfx_t* sfx;      // sfx number
    int leftvol;     // 0-255 volume
    int rightvol;    // 0-255 volume
    int end;         // end time in global paintsamples
    int pos;         // sample position in sfx
    int looping;     // where to loop, -1 = no looping
    int entnum;      // to allow overriding a specific sound
    int entchannel;  //
    Vector3 origin;   // origin of sound effect
    vec_t dist_mult; // distance multiplier (attenuation/clipK)
    int master_vol;  // 0-255 master volume
};

struct wavinfo_t {
    int rate;
    int width;
    int channels;
    int loopstart;
    int samples;
    int dataofs; // chunk starts this many bytes from file start
};

void S_Init(void);
void S_Startup(void);
void S_Shutdown(void);
void S_StartSound(int entnum,
    int entchannel,
    sfx_t* sfx,
    const Vector3& origin,
    float fvol,
    float attenuation);
void S_StaticSound(sfx_t* sfx, const Vector3& origin, float vol, float attenuation);
void S_StopSound(int entnum, int entchannel);
void S_StopAllSounds(bool clear);
void S_ClearBuffer(void);
void S_Update(const Vector3& origin, const Vector3& v_forward, const Vector3& v_right, const Vector3& v_up);
void S_ExtraUpdate(void);

[[nodiscard]] sfx_t* S_PrecacheSound(std::string_view sample);
void S_TouchSound(std::string_view sample);
void S_BeginPrecaching(void);
void S_EndPrecaching(void);
void S_PaintChannels(int endtime);
void S_InitPaintChannels(void);

// picks a channel based on priorities, empty slots, number of channels
[[nodiscard]] channel_t* SND_PickChannel(int entnum, int entchannel);

// spatializes a channel
void SND_Spatialize(channel_t* ch);

// initializes cycling through a DMA buffer and returns information on it
[[nodiscard]] bool SNDDMA_Init(void);

// shutdown the DMA xfer.
void SNDDMA_Shutdown(void);

// ====================================================================
// User-setable variables
// ====================================================================

inline constexpr int MAX_CHANNELS = 128;
inline constexpr int MAX_DYNAMIC_CHANNELS = 8;

extern vec_t sound_nominal_clip_dist;

extern cvar_t loadas8bit;
extern cvar_t bgmvolume;
extern cvar_t volume;

extern int snd_blocked;

void S_LocalSound(std::string_view s);
[[nodiscard]] sfxcache_t* S_LoadSound(sfx_t* s);

[[nodiscard]] wavinfo_t GetWavinfo(std::string_view name, std::span<const byte> wav_data);

void SND_InitScaletable(void);
void SNDDMA_Submit(void);

} // namespace Audio

// TODO: Refactor other engine subsystems to use explicit Audio:: scope, then remove these global using aliases.
using Audio::portable_samplepair_t;
using Audio::sfx_s;
using Audio::sfx_t;
using Audio::sfxcache_t;
using Audio::dma_t;
using Audio::channel_t;
using Audio::wavinfo_t;
