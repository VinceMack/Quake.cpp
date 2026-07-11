// cd_sdl.cpp -- CD audio stubs for SDL 2 compatibility

#include <SDL.h>

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


static qboolean cdValid = false;
static qboolean initialized = false;
static qboolean enabled = false;
static qboolean playLooping = false;
static float cdvolume = 1.0;
static int current_track = 0;

static void CDAudio_Eject()
{
    if (!enabled) {
        return;
    }

    // CD-ROM support removed in SDL 2
    Con_DPrintf("CD-ROM support is not available in SDL 2.\n");
}

namespace CDAudio {

void CDAudio_Play(byte track, qboolean looping)
{
    if (!enabled) {
        return;
    }

    // CD-ROM support removed in SDL 2
    // Store track info for compatibility
    if ((track < 1) || (track > 99)) {
        Con_DPrintf("CDAudio: Bad track number: %d\n", track);

        return;
    }

    current_track = track;
    playLooping = looping;

    Con_DPrintf("CD-ROM playback is not available in SDL 2.\n");
}

void CDAudio_Stop()
{
    if (!enabled) {
        return;
    }

    current_track = 0;
    playLooping = false;
}

void CDAudio_Pause()
{
    if (!enabled) {
        return;
    }

    // Stub implementation
}

void CDAudio_Resume()
{
    if (!enabled) {
        return;
    }

    // Stub implementation
}

void CDAudio_Update()
{
    if (!enabled) {
        return;
    }

    if (bgmvolume.value != cdvolume) {
        if (cdvolume) {
            Cvar::SetValue("bgmvolume", 0.0);
            CDAudio_Pause();
        } else {
            Cvar::SetValue("bgmvolume", 1.0);
            CDAudio_Resume();
        }

        cdvolume = bgmvolume.value;

        return;
    }
}

static void CD_f()
{
    std::string_view command;

    if (Cmd::Argc() < 2) {
        return;
    }

    command = Cmd::Argv(1);

    if (!Q_strcasecmp(command, "on")) {
        enabled = true;
        Con_Printf("CD Audio enabled (SDL 2 - no actual CD-ROM support).\n");

        return;
    }

    if (!Q_strcasecmp(command, "off")) {
        CDAudio_Stop();
        enabled = false;

        return;
    }

    if (!Q_strcasecmp(command, "play")) {
        CDAudio_Play(static_cast<byte>(Q_atoi(Cmd::Argv(2))), false);

        return;
    }

    if (!Q_strcasecmp(command, "loop")) {
        CDAudio_Play(static_cast<byte>(Q_atoi(Cmd::Argv(2))), true);

        return;
    }

    if (!Q_strcasecmp(command, "stop")) {
        CDAudio_Stop();

        return;
    }

    if (!Q_strcasecmp(command, "pause")) {
        CDAudio_Pause();

        return;
    }

    if (!Q_strcasecmp(command, "resume")) {
        CDAudio_Resume();

        return;
    }

    if (!Q_strcasecmp(command, "eject")) {
        CDAudio_Eject();

        return;
    }

    if (!Q_strcasecmp(command, "info")) {
        Con_Printf("CD-ROM support not available in SDL 2\n");
        if (current_track > 0) {
            Con_Printf("Would be %s track %d\n",
                playLooping ? "looping" : "playing", current_track);
        }

        return;
    }
}

int CDAudio_Init()
{
    if ((cls.state == ca_dedicated) || COM_CheckParm("-nocdaudio")) {
        return -1;
    }

    // SDL 2 removed CD-ROM support
    // Initialize as disabled but successful
    initialized = true;
    enabled = false;
    cdValid = false;

    Cmd::AddCommand("cd", CD_f);
    Con_Printf("CD Audio Initialized (SDL 2 - no CD-ROM support).\n");

    return 0;
}

void CDAudio_Shutdown()
{
    CDAudio_Stop();
    initialized = false;
}

} // namespace CDAudio
