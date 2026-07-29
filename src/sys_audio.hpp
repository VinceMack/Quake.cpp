// sys_audio.hpp -- Subsystem Audio Header
#pragma once

#include <EASTL/array.h>
#include <EASTL/string_view.h>
#include <EASTL/span.h>
#include <atomic>

#include "sys_core.hpp"

namespace Audio {

inline constexpr int DEFAULT_SOUND_PACKET_VOLUME = 255;
inline constexpr float DEFAULT_SOUND_PACKET_ATTENUATION = 1.0f;

struct portable_samplepair_t {
    int left;
    int right;
};

struct sfx_s {
    char name[MAX_QPATH];
    cache_user_t cache;
};

using sfx_t = sfx_s;

#pragma warning(push)
#pragma warning(disable: 4200)
struct sfxcache_t {
    int length;
    int loopstart;
    int speed;
    int width;
    int stereo;
    byte data[];
};
#pragma warning(pop)

struct dma_t {
    std::atomic<bool> gamealive;
    std::atomic<bool> soundalive;
    std::atomic<bool> splitbuffer;
    std::atomic<int> channels;
    std::atomic<int> samples;
    std::atomic<int> submission_chunk;
    std::atomic<int> samplepos;
    std::atomic<int> samplebits;
    std::atomic<int> speed;
    std::atomic<unsigned char*> buffer;
};

struct channel_t {
    sfx_t* sfx;
    int leftvol;
    int rightvol;
    int end;
    int pos;
    int looping;
    int entnum;
    int entchannel;
    Vector3 origin;
    vec_t dist_mult;
    int master_vol;
};

struct wavinfo_t {
    int rate;
    int width;
    int channels;
    int loopstart;
    int samples;
    int dataofs;
};

void S_Init(void);
void S_Startup(void);
void S_Shutdown(void);
void S_StartSound(int entnum, int entchannel, sfx_t* sfx, const Vector3& origin, float fvol, float attenuation);
void S_StaticSound(sfx_t* sfx, const Vector3& origin, float vol, float attenuation);
void S_StopSound(int entnum, int entchannel);
void S_StopAllSounds(bool clear);
void S_ClearBuffer(void);
void S_Update(const Vector3& origin, const Vector3& v_forward, const Vector3& v_right, const Vector3& v_up);
void S_ExtraUpdate(void);

[[nodiscard]] sfx_t* S_PrecacheSound(eastl::string_view sample);
void S_TouchSound(eastl::string_view sample);
void S_BeginPrecaching(void);
void S_EndPrecaching(void);
void S_PaintChannels(int endtime);
void S_InitPaintChannels(void);

[[nodiscard]] channel_t* SND_PickChannel(int entnum, int entchannel);
void SND_Spatialize(channel_t* ch);
[[nodiscard]] bool SNDDMA_Init(void);
void SNDDMA_Shutdown(void);

inline constexpr int MAX_CHANNELS = 128;
inline constexpr int MAX_DYNAMIC_CHANNELS = 8;

extern vec_t sound_nominal_clip_dist;

extern cvar_t loadas8bit;
extern cvar_t bgmvolume;
extern cvar_t volume;

extern int snd_blocked;

void S_LocalSound(eastl::string_view s);
[[nodiscard]] sfxcache_t* S_LoadSound(sfx_t* s);
[[nodiscard]] wavinfo_t GetWavinfo(eastl::string_view name, eastl::span<const byte> wav_data);
void SND_InitScaletable(void);
void SNDDMA_Submit(void);

} // namespace Audio

using Audio::portable_samplepair_t;
using Audio::sfx_s;
using Audio::sfx_t;
using Audio::sfxcache_t;
using Audio::dma_t;
using Audio::channel_t;
using Audio::wavinfo_t;
