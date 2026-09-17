// audio_main.hpp -- Audio Subsystem Orchestration, Spatialization, and Lifecycle
#pragma once

#include "audio/audio_types.hpp"
#include "core/cvar.hpp"
#include <vector>

namespace Audio {

extern SPSCQueue<AudioCommand, 256> command_queue;
extern float local_volume;
extern std::array<channel_t, MAX_CHANNELS> channels;
extern std::atomic<int> total_channels;
extern bool snd_ambient, sound_started, fakedma, snd_initialized;
extern Vector3 listener_origin, listener_forward, listener_right, listener_up;
extern int paintedtime;
// Never grows past MAX_SFX, so pointers into it stay valid (see S_FindName).
extern std::vector<sfx_t> known_sfx;
extern std::array<sfx_t*, NUM_AMBIENTS> ambient_sfx;

extern int snd_blocked;
extern vec_t sound_nominal_clip_dist;
extern cvar_t nosound, precache, bgmbuffer, ambient_level, ambient_fade;
extern cvar_t snd_noextraupdate, snd_show, _snd_mixahead;
extern cvar_t bgmvolume, volume, loadas8bit;

void S_Init();
void S_Startup();
void S_Shutdown();
void S_StartSound(int entnum, int entchannel, sfx_t* sfx, const Vector3& origin, float fvol, float attenuation);
void S_StaticSound(sfx_t* sfx, const Vector3& origin, float vol, float attenuation);
void S_StopSound(int entnum, int entchannel);
void S_StopAllSounds(bool clear);
void S_ClearBuffer();
void S_Update(const Vector3& origin, const Vector3& v_forward, const Vector3& v_right, const Vector3& v_up);

[[nodiscard]] sfx_t* S_PrecacheSound(std::string_view sample);
inline void S_BeginPrecaching() { }
inline void S_EndPrecaching() { }
void S_LocalSound(std::string_view s);
[[nodiscard]] sfx_t* S_FindName(std::string_view name);

[[nodiscard]] channel_t* SND_PickChannel(int entnum, int entchannel);
void SND_Spatialize(channel_t* ch);

void PushAudioCommand(const AudioCommand& cmd);
void ExecuteAudioCommand(const AudioCommand& cmd);

void S_StartSoundInternal(
    int entnum, int entchannel, sfx_t* sfx, const Vector3& origin, float fvol, float attenuation, int random_offset);
void S_StaticSoundInternal(sfx_t* sfx, const Vector3& origin, float vol, float attenuation);
void S_StopSoundInternal(int entnum, int entchannel);
void S_StopAllSoundsInternal(bool clear);
void S_UpdateInternal(const Vector3& origin, const Vector3& forward, const Vector3& right, const Vector3& up,
    float vol_val, const std::array<int, NUM_AMBIENTS>& ambient_vols, float host_frametime_val, float ambient_fade_val,
    bool snd_ambient_val);

void S_Play();
void S_PlayVol();
void S_SoundList();
void S_SoundInfo_f();

} // namespace Audio
