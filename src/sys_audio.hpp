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
inline constexpr int MAX_CHANNELS = 128;
inline constexpr int MAX_DYNAMIC_CHANNELS = 8;

struct portable_samplepair_t { int left{}; int right{}; };
struct sfx_t { char name[MAX_QPATH]{}; cache_user_t cache{}; };
using sfx_s = sfx_t;

#pragma warning(push)
#pragma warning(disable: 4200)
struct sfxcache_t { int length{}; int loopstart{}; int speed{}; int width{}; int stereo{}; byte data[]; };
#pragma warning(pop)

struct dma_t {
    std::atomic<bool> gamealive{false}, soundalive{false}, splitbuffer{false};
    std::atomic<int> channels{0}, samples{0}, submission_chunk{0}, samplepos{0}, samplebits{0}, speed{0};
    std::atomic<unsigned char*> buffer{nullptr};
    void Reset(int bits, int spd, int ch, int smp, unsigned char* buf = nullptr) {
        splitbuffer.store(0, std::memory_order_relaxed); samplebits.store(bits, std::memory_order_relaxed);
        speed.store(spd, std::memory_order_relaxed); channels.store(ch, std::memory_order_relaxed);
        samples.store(smp, std::memory_order_relaxed); samplepos.store(0, std::memory_order_relaxed);
        soundalive.store(true, std::memory_order_relaxed); gamealive.store(true, std::memory_order_relaxed);
        submission_chunk.store(1, std::memory_order_relaxed); buffer.store(buf, std::memory_order_release);
    }
};

struct channel_t {
    sfx_t* sfx{};
    int leftvol{}, rightvol{}, end{}, pos{}, looping{}, entnum{}, entchannel{};
    Vector3 origin{};
    vec_t dist_mult{};
    int master_vol{};
};

struct wavinfo_t { int rate{}, width{}, channels{}, loopstart{}, samples{}, dataofs{}; };

void S_Init();
void S_Startup();
void S_Shutdown();
void S_StartSound(int entnum, int entchannel, sfx_t* sfx, const Vector3& origin, float fvol, float attenuation);
void S_StaticSound(sfx_t* sfx, const Vector3& origin, float vol, float attenuation);
void S_StopSound(int entnum, int entchannel);
void S_StopAllSounds(bool clear);
void S_ClearBuffer();
void S_Update(const Vector3& origin, const Vector3& v_forward, const Vector3& v_right, const Vector3& v_up);
void S_ExtraUpdate();

[[nodiscard]] sfx_t* S_PrecacheSound(eastl::string_view sample);
void S_TouchSound(eastl::string_view sample);
inline void S_BeginPrecaching() {}
inline void S_EndPrecaching() {}
void S_PaintChannels(int endtime);

[[nodiscard]] channel_t* SND_PickChannel(int entnum, int entchannel);
void SND_Spatialize(channel_t* ch);
[[nodiscard]] bool SNDDMA_Init();
void SNDDMA_Shutdown();

extern vec_t sound_nominal_clip_dist;
extern cvar_t loadas8bit, bgmvolume, volume;
extern int snd_blocked;

void S_LocalSound(eastl::string_view s);
[[nodiscard]] sfxcache_t* S_LoadSound(sfx_t* s);
[[nodiscard]] wavinfo_t GetWavinfo(eastl::string_view name, eastl::span<const byte> wav_data);
void SND_InitScaletable();

} // namespace Audio

using Audio::portable_samplepair_t;
using Audio::sfx_s;
using Audio::sfx_t;
using Audio::sfxcache_t;
using Audio::dma_t;
using Audio::channel_t;
using Audio::wavinfo_t;
