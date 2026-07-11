// audio.cpp -- merged audio subsystem (snd_*.cpp)
// Contains: sound control, caching, mixing, and SDL audio driver

#include "quakedef.hpp"

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

// ============================================================================
// snd_dma.cpp -- main control for any streaming sound output device
// ============================================================================

void S_Play(void);
void S_PlayVol(void);
void S_SoundList(void);
void S_Update_();
void S_StopAllSounds(qboolean clear);

// =======================================================================
// Internal sound data & structures
// =======================================================================

channel_t channels[MAX_CHANNELS];
int total_channels;

int snd_blocked = 0;
static qboolean snd_ambient = 1;
qboolean snd_initialized = false;

// pointer should go away
volatile dma_t* shm = 0;
volatile dma_t sn;

Vector3 listener_origin;
Vector3 listener_forward;
Vector3 listener_right;
Vector3 listener_up;
vec_t sound_nominal_clip_dist = 1000.0;

int soundtime;   // sample PAIRS
int paintedtime; // sample PAIRS

#define MAX_SFX 512
sfx_t* known_sfx; // hunk allocated [MAX_SFX]
int num_sfx;

sfx_t* ambient_sfx[NUM_AMBIENTS];

int desired_speed = 11025;
int desired_bits = 16;

int sound_started = 0;

cvar_t bgmvolume = { "bgmvolume", "1", true, {}, {}, {} };
cvar_t volume = { "volume", "0.7", true, {}, {}, {} };

cvar_t nosound = { "nosound", "0", {}, {}, {}, {} };
cvar_t precache = { "precache", "1", {}, {}, {}, {} };
cvar_t loadas8bit = { "loadas8bit", "0", {}, {}, {}, {} };
cvar_t bgmbuffer = { "bgmbuffer", "4096", {}, {}, {}, {} };
cvar_t ambient_level = { "ambient_level", "0.3", {}, {}, {}, {} };
cvar_t ambient_fade = { "ambient_fade", "100", {}, {}, {}, {} };
cvar_t snd_noextraupdate = { "snd_noextraupdate", "0", {}, {}, {}, {} };
cvar_t snd_show = { "snd_show", "0", {}, {}, {}, {} };
cvar_t _snd_mixahead = { "_snd_mixahead", "0.1", true, {}, {}, {} };

// ====================================================================
// User-setable variables
// ====================================================================

qboolean fakedma = false;

void S_SoundInfo_f(void)
{
    if (!sound_started || !shm) {
        Con_Printf("sound system not started\n");

        return;
    }

    Con_Printf("%5d stereo\n", shm->channels - 1);
    Con_Printf("%5d samples\n", shm->samples);
    Con_Printf("%5d samplepos\n", shm->samplepos);
    Con_Printf("%5d samplebits\n", shm->samplebits);
    Con_Printf("%5d submission_chunk\n", shm->submission_chunk);
    Con_Printf("%5d speed\n", shm->speed);
    Con_Printf("0x%x dma buffer\n", shm->buffer);
    Con_Printf("%5d total_channels\n", total_channels);
}

/*
================
S_Startup
================
*/

void S_Startup(void)
{
    int rc;

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

    known_sfx = (sfx_t *) Hunk_Alloc(MAX_SFX * sizeof(sfx_t), "sfx_t");
    num_sfx = 0;

    // create a piece of DMA memory

    if (fakedma) {
        shm = (volatile dma_t*)(void*)Hunk_Alloc(sizeof(*shm), "shm");
        shm->splitbuffer = 0;
        shm->samplebits = 16;
        shm->speed = 22050;
        shm->channels = 2;
        shm->samples = 32768;
        shm->samplepos = 0;
        shm->soundalive = true;
        shm->gamealive = true;
        shm->submission_chunk = 1;
        shm->buffer = (unsigned char *) Hunk_Alloc(1 << 16, "shmbuf");
    }

    if (shm) {
        Con_Printf("Sound sampling rate: %i\n", shm->speed);
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
        shm->gamealive = 0;
    }

    shm = 0;
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
sfx_t* S_FindName(const char* name)
{
    int i;
    sfx_t* sfx;

    if (!name) {
        Sys_Error("S_FindName: NULL\n");
    }

    if (Q_strlen(name) >= MAX_QPATH) {
        Sys_Error("Sound name too long: %s", name);
    }

    // see if already loaded
    for (i = 0; i < num_sfx; i++) {
        if (!Q_strcmp(known_sfx[i].name, name)) {
            return &known_sfx[i];
        }
    }

    if (num_sfx == MAX_SFX) {
        Sys_Error("S_FindName: out of sfx_t");
    }

    sfx = &known_sfx[i];
    strcpy_s(sfx->name, sizeof(sfx->name), name);

    num_sfx++;

    return sfx;
}

/*
==================
S_TouchSound

==================
*/
void S_TouchSound(const char* name)
{
    sfx_t* sfx;

    if (!sound_started) {
        return;
    }

    sfx = S_FindName(name);
    Cache_Check(&sfx->cache);
}

/*
==================
S_PrecacheSound

==================
*/
sfx_t* S_PrecacheSound(const char* name)
{
    sfx_t* sfx;

    if (!sound_started || nosound.value) {
        return NULL;
    }

    sfx = S_FindName(name);

    // cache it in
    if (precache.value) {
        S_LoadSound(sfx);
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
    int ch_idx;
    int first_to_die;
    int life_left;

    // Check for replacement sound, or find the best one to replace
    first_to_die = -1;
    life_left = 0x7fffffff;
    for (ch_idx = NUM_AMBIENTS; ch_idx < NUM_AMBIENTS + MAX_DYNAMIC_CHANNELS;
        ch_idx++) {
        if (entchannel != 0                                                                                            // channel 0 never overrides
            && channels[ch_idx].entnum == entnum && (channels[ch_idx].entchannel == entchannel || entchannel == -1)) { // allways override sound from same entity
            first_to_die = ch_idx;
            break;
        }

        // don't let monster sounds override player sounds
        if (channels[ch_idx].entnum == cl.viewentity && entnum != cl.viewentity && channels[ch_idx].sfx) {
            continue;
        }

        if (channels[ch_idx].end - paintedtime < life_left) {
            life_left = channels[ch_idx].end - paintedtime;
            first_to_die = ch_idx;
        }
    }

    if (first_to_die == -1) {
        return NULL;
    }

    if (channels[first_to_die].sfx) {
        channels[first_to_die].sfx = NULL;
    }

    return &channels[first_to_die];
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

    if (shm->channels == 1) {
        rscale = 1.0;
        lscale = 1.0;
    } else {
        rscale = static_cast<vec_t>(1.0 + dot);
        lscale = static_cast<vec_t>(1.0 - dot);
    }

    // add in distance effect
    scale = static_cast<vec_t>((1.0 - dist) * rscale);
    ch->rightvol = (int)(ch->master_vol * scale);
    if (ch->rightvol < 0) {
        ch->rightvol = 0;
    }

    scale = static_cast<vec_t>((1.0 - dist) * lscale);
    ch->leftvol = (int)(ch->master_vol * scale);
    if (ch->leftvol < 0) {
        ch->leftvol = 0;
    }
}

// =======================================================================
// Start a sound effect
// =======================================================================

void S_StartSound(int entnum,
    int entchannel,
    sfx_t* sfx,
    const Vector3& origin,
    float fvol,
    float attenuation)
{
    channel_t *target_chan, *check;
    sfxcache_t* sc;
    int vol;
    int ch_idx;
    int skip;

    if (!sound_started) {
        return;
    }

    if (!sfx) {
        return;
    }

    if (nosound.value) {
        return;
    }

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

    // new channel
    sc = S_LoadSound(sfx);
    if (!sc) {
        target_chan->sfx = NULL;

        return; // couldn't load the sound's data
    }

    target_chan->sfx = sfx;
    target_chan->pos = static_cast<int>(0.0);
    target_chan->end = paintedtime + sc->length;

    // if an identical sound has also been started this frame, offset the pos
    // a bit to keep it from just making the first one louder
    check = &channels[NUM_AMBIENTS];
    for (ch_idx = NUM_AMBIENTS; ch_idx < NUM_AMBIENTS + MAX_DYNAMIC_CHANNELS;
        ch_idx++, check++) {
        if (check == target_chan) {
            continue;
        }

        if (check->sfx == sfx && !check->pos) {
            skip = rand() % (int)(0.1 * shm->speed);
            if (skip >= target_chan->end) {
                skip = target_chan->end - 1;
            }

            target_chan->pos += skip;
            target_chan->end -= skip;
            break;
        }
    }
}

void S_StopSound(int entnum, int entchannel)
{
    int i;

    for (i = 0; i < MAX_DYNAMIC_CHANNELS; i++) {
        if (channels[i].entnum == entnum && channels[i].entchannel == entchannel) {
            channels[i].end = 0;
            channels[i].sfx = NULL;

            return;
        }
    }
}

void S_StopAllSounds(qboolean clear)
{
    int i;

    if (!sound_started) {
        return;
    }

    total_channels = MAX_DYNAMIC_CHANNELS + NUM_AMBIENTS; // no statics

    for (i = 0; i < MAX_CHANNELS; i++) {
        if (channels[i].sfx) {
            channels[i].sfx = NULL;
        }
    }

    Q_memset(channels, 0, MAX_CHANNELS * sizeof(channel_t));

    if (clear) {
        S_ClearBuffer();
    }
}



void S_ClearBuffer(void)
{
#if 0
    int clear;

    if (shm->samplebits == 8) {
        clear = 0x80;
    } else {
        clear = 0;
    }

    {
        Q_memset(shm->buffer, clear, shm->samples * shm->samplebits / 8);
    }
#endif
}

/*
=================
S_StaticSound
=================
*/
void S_StaticSound(sfx_t* sfx, const Vector3& origin, float vol, float attenuation)
{
    channel_t* ss;
    sfxcache_t* sc;

    if (!sfx) {
        return;
    }

    if (total_channels == MAX_CHANNELS) {
        Con_Printf("total_channels == MAX_CHANNELS\n");

        return;
    }

    ss = &channels[total_channels];
    total_channels++;

    sc = S_LoadSound(sfx);
    if (!sc) {
        return;
    }

    if (sc->loopstart == -1) {
        Con_Printf("Sound %s not looped\n", sfx->name);

        return;
    }

    ss->sfx = sfx;
    ss->origin = origin;
    ss->master_vol = static_cast<int>(vol);
    ss->dist_mult = (attenuation / 64) / sound_nominal_clip_dist;
    ss->end = paintedtime + sc->length;

    SND_Spatialize(ss);
}

//=============================================================================

/*
===================
S_UpdateAmbientSounds
===================
*/
void S_UpdateAmbientSounds(void)
{
    mleaf_t* l;
    float vol;
    int ambient_channel;
    channel_t* chan;

    if (!snd_ambient) {
        return;
    }

    // calc ambient sound levels
    if (!cl.worldmodel) {
        return;
    }

    l = Mod_PointInLeaf(listener_origin, cl.worldmodel);
    if (!l || !ambient_level.value) {
        for (ambient_channel = 0; ambient_channel < NUM_AMBIENTS;
            ambient_channel++) {
            channels[ambient_channel].sfx = NULL;
        }

        return;
    }

    for (ambient_channel = 0; ambient_channel < NUM_AMBIENTS; ambient_channel++) {
        chan = &channels[ambient_channel];
        chan->sfx = ambient_sfx[ambient_channel];

        vol = ambient_level.value * l->ambient_sound_level[ambient_channel];
        if (vol < 8) {
            vol = 0;
        }

        // don't adjust volume too fast
        if (chan->master_vol < vol) {
            chan->master_vol = static_cast<int>(chan->master_vol + host_frametime * ambient_fade.value);
            if (chan->master_vol > vol) {
                chan->master_vol = static_cast<int>(vol);
            }
        } else if (chan->master_vol > vol) {
            chan->master_vol = static_cast<int>(chan->master_vol - host_frametime * ambient_fade.value);
            if (chan->master_vol < vol) {
                chan->master_vol = static_cast<int>(vol);
            }
        }

        chan->leftvol = chan->rightvol = chan->master_vol;
    }
}

/*
============
S_Update

Called once each time through the main loop
============
*/
void S_Update(const Vector3& origin, const Vector3& forward, const Vector3& right, const Vector3& up)
{
    int i, j;
    int total;
    channel_t* ch;
    channel_t* combine;

    if (!sound_started || (snd_blocked > 0)) {
        return;
    }

    listener_origin = origin;
    listener_forward = forward;
    listener_right = right;
    listener_up = up;

    // update general area ambient sound sources
    S_UpdateAmbientSounds();

    combine = NULL;

    // update spatialization for static and dynamic sounds
    ch = channels + NUM_AMBIENTS;
    for (i = NUM_AMBIENTS; i < total_channels; i++, ch++) {
        if (!ch->sfx) {
            continue;
        }

        SND_Spatialize(ch); // respatialize channel
        if (!ch->leftvol && !ch->rightvol) {
            continue;
        }

        // try to combine static sounds with a previous channel of the same
        // sound effect so we don't mix five torches every frame

        if (i >= MAX_DYNAMIC_CHANNELS + NUM_AMBIENTS) {
            // see if it can just use the last one
            if (combine && combine->sfx == ch->sfx) {
                combine->leftvol += ch->leftvol;
                combine->rightvol += ch->rightvol;
                ch->leftvol = ch->rightvol = 0;
                continue;
            }

            // search for one
            combine = channels + MAX_DYNAMIC_CHANNELS + NUM_AMBIENTS;
            for (j = MAX_DYNAMIC_CHANNELS + NUM_AMBIENTS; j < i; j++, combine++) {
                if (combine->sfx == ch->sfx) {
                    break;
                }
            }

            if (j == total_channels) {
                combine = NULL;
            } else {
                if (combine != ch) {
                    combine->leftvol += ch->leftvol;
                    combine->rightvol += ch->rightvol;
                    ch->leftvol = ch->rightvol = 0;
                }

                continue;
            }
        }
    }

    //
    // debugging output
    //
    if (snd_show.value) {
        total = 0;
        ch = channels;
        for (i = 0; i < total_channels; i++, ch++) {
            if (ch->sfx && (ch->leftvol || ch->rightvol)) {
                //Con_Printf ("%3i %3i %s\n", ch->leftvol, ch->rightvol, ch->sfx->name);
                total++;
            }
        }

        Con_Printf("----(%i)----\n", total);
    }

    // mix some sound
    S_Update_();
}

void S_ExtraUpdate(void)
{

    if (snd_noextraupdate.value) {
        return; // don't pollute timings
    }

    S_Update_();
}

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
    static int hash = 345;
    int i;
    char name[256];
    sfx_t* sfx;

    i = 1;
    while (i < Cmd::Argc()) {
        if (Cmd::Argv(i).find('.') == std::string_view::npos) {
            Q_strcpy(name, Cmd::Argv(i));
            Q_strcat(name, ".wav");
        } else {
            Q_strcpy(name, Cmd::Argv(i));
        }

        sfx = S_PrecacheSound(name);
        S_StartSound(hash++, 0, sfx, listener_origin, 1.0, 1.0);
        i++;
    }
}

void S_PlayVol(void)
{
    static int hash = 543;
    int i;
    float vol;
    char name[256];
    sfx_t* sfx;

    i = 1;
    while (i < Cmd::Argc()) {
        if (Cmd::Argv(i).find('.') == std::string_view::npos) {
            Q_strcpy(name, Cmd::Argv(i));
            Q_strcat(name, ".wav");
        } else {
            Q_strcpy(name, Cmd::Argv(i));
        }

        sfx = S_PrecacheSound(name);
        vol = Q_atof(Cmd::Argv(i + 1));
        S_StartSound(hash++, 0, sfx, listener_origin, vol, 1.0);
        i += 2;
    }
}

void S_SoundList(void)
{
    int i;
    sfx_t* sfx;
    sfxcache_t* sc;
    int size, total;

    total = 0;
    for (sfx = known_sfx, i = 0; i < num_sfx; i++, sfx++) {
        sc = (sfxcache_t *) Cache_Check(&sfx->cache);
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

        Con_Printf("(%2db) %6i : %s\n", sc->width * 8, size, sfx->name);
    }
    Con_Printf("Total resident: %i\n", total);
}

void S_LocalSound(const char* sound)
{
    sfx_t* sfx;

    if (nosound.value) {
        return;
    }

    if (!sound_started) {
        return;
    }

    sfx = S_PrecacheSound(sound);
    if (!sfx) {
        Con_Printf("S_LocalSound: can't cache %s\n", sound);

        return;
    }

    S_StartSound(cl.viewentity, -1, sfx, vec3_origin, 1, 1);
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
void ResampleSfx(sfx_t* sfx, int inrate, int inwidth, byte* data)
{
    int outcount;
    int srcsample;
    float stepscale;
    int i;
    int sample, samplefrac, fracstep;
    sfxcache_t* sc;

    sc = (sfxcache_t *) Cache_Check(&sfx->cache);
    if (!sc) {
        return;
    }

    stepscale = (float)inrate / shm->speed; // this is usually 0.5, 1, or 2

    outcount = static_cast<int>(sc->length / stepscale);
    sc->length = outcount;
    if (sc->loopstart != -1) {
        sc->loopstart = static_cast<int>(sc->loopstart / stepscale);
    }

    sc->speed = shm->speed;
    if (loadas8bit.value) {
        sc->width = 1;
    } else {
        sc->width = inwidth;
    }

    sc->stereo = 0;

    // resample / decimate to the current source rate

    if (stepscale == 1 && inwidth == 1 && sc->width == 1) {
        // fast special case
        for (i = 0; i < outcount; i++) {
            ((signed char*)sc->data)[i] = (int)((unsigned char)(data[i]) - 128);
        }
    } else {
        // general case
        samplefrac = 0;
        fracstep = static_cast<int>(stepscale * 256);
        for (i = 0; i < outcount; i++) {
            srcsample = samplefrac >> 8;
            samplefrac += fracstep;
            if (inwidth == 2) {
                sample = LittleShort(((short*)data)[srcsample]);
            } else {
                sample = (int)((unsigned char)(data[srcsample]) - 128) << 8;
            }

            if (sc->width == 2) {
                ((short*)sc->data)[i] = static_cast<short>(sample);
            } else {
                ((signed char*)sc->data)[i] = static_cast<signed char>(sample >> 8);
            }
        }
    }
}

//=============================================================================

/*
==============
S_LoadSound
==============
*/
sfxcache_t* S_LoadSound(sfx_t* s)
{
    char namebuffer[256];
    byte* data;
    wavinfo_t info;
    int len;
    float stepscale;
    sfxcache_t* sc;
    byte stackbuf[1 * 1024]; // avoid dirtying the cache heap

    // see if still in memory
    sc = (sfxcache_t *) Cache_Check(&s->cache);
    if (sc) {
        return sc;
    }

    //Con_Printf ("S_LoadSound: %x\n", (int)stackbuf);
    // load it in
    Q_strcpy(namebuffer, "sound/");
    Q_strcat(namebuffer, s->name);

    //	Con_Printf ("loading %s\n",namebuffer);

    data = COM_LoadStackFile(namebuffer, stackbuf, sizeof(stackbuf));

    if (!data) {
        Con_Printf("Couldn't load %s\n", namebuffer);

        return NULL;
    }

    info = GetWavinfo(s->name, data, com_filesize);
    if (info.channels != 1) {
        Con_Printf("%s is a stereo sample\n", s->name);

        return NULL;
    }

    stepscale = (float)info.rate / shm->speed;
    len = static_cast<int>(info.samples / stepscale);

    len = len * info.width * info.channels;

    sc = (sfxcache_t *) Cache_Alloc(&s->cache, len + sizeof(sfxcache_t), s->name);
    if (!sc) {
        return NULL;
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

byte* data_p;
byte* iff_end;
byte* last_chunk;
byte* iff_data;
int iff_chunk_len;

short GetLittleShort(void)
{
    short val = 0;
    val = *data_p;
    val = val + (*(data_p + 1) << 8);
    data_p += 2;

    return val;
}

int GetLittleLong(void)
{
    int val = 0;
    val = *data_p;
    val = val + (*(data_p + 1) << 8);
    val = val + (*(data_p + 2) << 16);
    val = val + (*(data_p + 3) << 24);
    data_p += 4;

    return val;
}

void FindNextChunk(const char* name)
{
    while (1) {
        data_p = last_chunk;

        if (data_p >= iff_end) { // didn't find the chunk
            data_p = NULL;

            return;
        }

        data_p += 4;
        iff_chunk_len = GetLittleLong();
        if (iff_chunk_len < 0) {
            data_p = NULL;

            return;
        }

        //		if (iff_chunk_len > 1024*1024)
        //			Sys_Error ("FindNextChunk: %i length is past the 1 meg sanity limit", iff_chunk_len);
        data_p -= 8;
        last_chunk = data_p + 8 + ((iff_chunk_len + 1) & ~1);
        if (!Q_strncmp((char*)data_p, name, 4)) {
            return;
        }
    }
}

void FindChunk(const char* name)
{
    last_chunk = iff_data;
    FindNextChunk(name);
}

/*
============
GetWavinfo
============
*/
wavinfo_t GetWavinfo(char* name, byte* wav, int wavlength)
{
    wavinfo_t info;
    int i;
    int format;
    int samples;

    memset(&info, 0, sizeof(info));

    if (!wav) {
        return info;
    }

    iff_data = wav;
    iff_end = wav + wavlength;

    // find "RIFF" chunk
    FindChunk("RIFF");
    if (!(data_p && !Q_strncmp((char*)data_p + 8, "WAVE", 4))) {
        Con_Printf("Missing RIFF/WAVE chunks\n");

        return info;
    }

    // get "fmt " chunk
    iff_data = data_p + 12;
    // DumpChunks ();

    FindChunk("fmt ");
    if (!data_p) {
        Con_Printf("Missing fmt chunk\n");

        return info;
    }

    data_p += 8;
    format = GetLittleShort();
    if (format != 1) {
        Con_Printf("Microsoft PCM format only\n");

        return info;
    }

    info.channels = GetLittleShort();
    info.rate = GetLittleLong();
    data_p += 4 + 2;
    info.width = GetLittleShort() / 8;

    // get cue chunk
    FindChunk("cue ");
    if (data_p) {
        data_p += 32;
        info.loopstart = GetLittleLong();
        //		Con_Printf("loopstart=%d\n", sfx->loopstart);

        // if the next chunk is a LIST chunk, look for a cue length marker
        FindNextChunk("LIST");
        if (data_p) {
            if (!strncmp(
                    (char*)data_p + 28, "mark",
                    4)) { // this is not a proper parse, but it works with cooledit...
                data_p += 24;
                i = GetLittleLong(); // samples in loop
                info.samples = info.loopstart + i;
                //				Con_Printf("looped length: %i\n", i);
            }
        }
    } else {
        info.loopstart = -1;
    }

    // find data chunk
    FindChunk("data");
    if (!data_p) {
        Con_Printf("Missing data chunk\n");

        return info;
    }

    data_p += 4;
    samples = GetLittleLong() / info.width;

    if (info.samples) {
        if (samples < info.samples) {
            Sys_Error("Sound %s has a bad loop length", name);
        }
    } else {
        info.samples = samples;
    }

    info.dataofs = static_cast<int>(data_p - wav);

    return info;
}

// ============================================================================
// snd_mix.cpp -- portable code to mix sounds
// ============================================================================

#include <stdint.h>

#define PAINTBUFFER_SIZE 512
portable_samplepair_t paintbuffer[PAINTBUFFER_SIZE];
int snd_scaletable[32][256];
int *snd_p, snd_linear_count, snd_vol;
short* snd_out;

void Snd_WriteLinearBlastStereo16(void);

void Snd_WriteLinearBlastStereo16(void)
{
    int i;
    int val;

    for (i = 0; i < snd_linear_count; i += 2) {
        val = (snd_p[i] * snd_vol) >> 8;
        if (val > 0x7fff) {
            snd_out[i] = 0x7fff;
        } else if (val < -32768) {
            snd_out[i] = -32768;
        } else {
            snd_out[i] = static_cast<short>(val);
        }

        val = (snd_p[i + 1] * snd_vol) >> 8;
        if (val > 0x7fff) {
            snd_out[i + 1] = 0x7fff;
        } else if (val < -32768) {
            snd_out[i + 1] = -32768;
        } else {
            snd_out[i + 1] = static_cast<short>(val);
        }
    }
}

void S_TransferStereo16(int endtime)
{
    int lpos;
    int lpaintedtime;
    unsigned char* pbuf;

    snd_vol = static_cast<int>(volume.value * 256);

    snd_p = (int*)paintbuffer;
    lpaintedtime = paintedtime;

    {
        pbuf = (unsigned char*)shm->buffer;
    }

    while (lpaintedtime < endtime) {
        // handle recirculating buffer issues
        lpos = lpaintedtime & ((shm->samples >> 1) - 1);

        snd_out = (short*)pbuf + (lpos << 1);

        snd_linear_count = (shm->samples >> 1) - lpos;
        if (lpaintedtime + snd_linear_count > endtime) {
            snd_linear_count = endtime - lpaintedtime;
        }

        snd_linear_count <<= 1;

        // write a linear blast of samples
        Snd_WriteLinearBlastStereo16();

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

    if (shm->samplebits == 16 && shm->channels == 2) {
        S_TransferStereo16(endtime);

        return;
    }

    p = (int*)paintbuffer;
    count = (endtime - paintedtime) * shm->channels;
    out_mask = shm->samples - 1;
    out_idx = paintedtime * shm->channels & out_mask;
    step = 3 - shm->channels;
    mix_vol = static_cast<int>(volume.value * 256);

    {
        pbuf = (unsigned char*)shm->buffer;
    }

    if (shm->samplebits == 16) {
        short* out = (short*)pbuf;
        while (count--) {
            val = (*p * mix_vol) >> 8;
            p += step;
            if (val > 0x7fff) {
                val = 0x7fff;
            } else if (val < -32768) {
                val = -32768;
            }

            out[out_idx] = static_cast<short>(val);
            out_idx = (out_idx + 1) & out_mask;
        }
    } else if (shm->samplebits == 8) {
        unsigned char* out = (unsigned char*)pbuf;
        while (count--) {
            val = (*p * mix_vol) >> 8;
            p += step;
            if (val > 0x7fff) {
                val = 0x7fff;
            } else if (val < -32768) {
                val = -32768;
            }

            out[out_idx] = static_cast<unsigned char>((val >> 8) + 128);
            out_idx = (out_idx + 1) & out_mask;
        }
    }

}

/*
===============================================================================

CHANNEL MIXING

===============================================================================
*/

void SND_PaintChannelFrom8(channel_t* ch, sfxcache_t* sc, int count, int offset);
void SND_PaintChannelFrom16(channel_t* ch, sfxcache_t* sc, int count, int offset);

void S_PaintChannels(int endtime)
{
    int i;
    int end;
    channel_t* ch;
    sfxcache_t* sc;
    int ltime, count;

    while (paintedtime < endtime) {
        // if paintbuffer is smaller than DMA buffer
        end = endtime;
        if (endtime - paintedtime > PAINTBUFFER_SIZE) {
            end = paintedtime + PAINTBUFFER_SIZE;
        }

        // clear the paint buffer
        Q_memset(paintbuffer, 0,
            (end - paintedtime) * sizeof(portable_samplepair_t));

        // paint in the channels.
        ch = channels;
        for (i = 0; i < total_channels; i++, ch++) {
            if (!ch->sfx) {
                continue;
            }

            if (!ch->leftvol && !ch->rightvol) {
                continue;
            }

            sc = S_LoadSound(ch->sfx);
            if (!sc) {
                continue;
            }

            ltime = paintedtime;

            while (ltime < end) { // paint up to end
                if (ch->end < end) {
                    count = ch->end - ltime;
                } else {
                    count = end - ltime;
                }

                if (count > 0) {
                    if (sc->width == 1) {
                        SND_PaintChannelFrom8(ch, sc, count, ltime - paintedtime);
                    } else {
                        SND_PaintChannelFrom16(ch, sc, count, ltime - paintedtime);
                    }

                    ltime += count;
                }

                // if at end of loop, restart
                if (ltime >= ch->end) {
                    if (sc->loopstart >= 0) {
                        ch->pos = sc->loopstart;
                        ch->end = ltime + sc->length - ch->pos;
                    } else { // channel just stopped
                        ch->sfx = NULL;
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
    int i, j;

    for (i = 0; i < 32; i++) {
        for (j = 0; j < 256; j++) {
            snd_scaletable[i][j] = ((signed char)j) * i * 8;
        }
    }
}

void SND_PaintChannelFrom8(channel_t* ch, sfxcache_t* sc, int count, int offset)
{
    int data;
    int *lscale, *rscale;
    unsigned char* sfx;
    int i;

    if (ch->leftvol > 255) {
        ch->leftvol = 255;
    }

    if (ch->rightvol > 255) {
        ch->rightvol = 255;
    }

    lscale = snd_scaletable[ch->leftvol >> 3];
    rscale = snd_scaletable[ch->rightvol >> 3];
    sfx = (unsigned char*)sc->data + ch->pos;

    for (i = 0; i < count; i++) {
        data = sfx[i];
        paintbuffer[offset + i].left += lscale[data];
        paintbuffer[offset + i].right += rscale[data];
    }

    ch->pos += count;
}

void SND_PaintChannelFrom16(channel_t* ch, sfxcache_t* sc, int count, int offset)
{
    int data;
    int left, right;
    int leftvol, rightvol;
    signed short* sfx;
    int i;

    leftvol = ch->leftvol;
    rightvol = ch->rightvol;
    sfx = (signed short*)sc->data + ch->pos;

    for (i = 0; i < count; i++) {
        data = sfx[i];
        left = (data * leftvol) >> 8;
        right = (data * rightvol) >> 8;
        paintbuffer[offset + i].left += left;
        paintbuffer[offset + i].right += right;
    }

    ch->pos += count;
}

// ============================================================================
// snd_sdl.cpp -- SDL audio output driver
// ============================================================================

#include <stdio.h>
#include <SDL.h>

static dma_t the_shm;
static int snd_inited;

static void paint_audio(void* /*unused*/, Uint8* stream, int len)
{
    if (shm) {
        shm->buffer = stream;
        shm->samplepos += len / (shm->samplebits / 8) / 2;
        // Check for samplepos overflow?
        S_PaintChannels(shm->samplepos);
    }
}

qboolean SNDDMA_Init(void)
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
        if (SDL_BYTEORDER == SDL_BIG_ENDIAN) {
            desired.format = AUDIO_S16MSB;
        } else {
            desired.format = AUDIO_S16LSB;
        }

        break;
    default:
        Con_Printf("Unknown number of audio bits: %d\n", desired_bits);

        return 0;
    }
    desired.channels = 2;
    desired.samples = 512;
    desired.callback = paint_audio;

    /* Open the audio device */
    if (SDL_OpenAudio(&desired, NULL) < 0) {
        Con_Printf("Couldn't open SDL audio: %s\n", SDL_GetError());

        return 0;
    }
    SDL_PauseAudio(0);

    /* Fill the audio DMA information block */
    shm = &the_shm;
    shm->splitbuffer = 0;
    shm->samplebits = (desired.format & 0xFF);
    shm->speed = desired.freq;
    shm->channels = desired.channels;
    shm->samples = desired.samples * shm->channels;
    shm->samplepos = 0;
    shm->submission_chunk = 1;
    shm->buffer = NULL;

    snd_inited = 1;

    return 1;
}

void SNDDMA_Shutdown(void)
{
    if (snd_inited) {
        SDL_CloseAudio();
        snd_inited = 0;
    }
}

} // namespace Audio
