// main.cpp -- Process entry point and the top-level frame loop
#include "quakedef.hpp"

#include <SDL.h>
#include <csignal>
#include <cstdlib>

namespace {

constexpr int kDefaultHeapSize = 64 * 1024 * 1024;

// Runs exactly `frames` frames at the fixed server tick rate, prints a hash of the
// resulting game state, and exits. Used by tests/regression.py to detect behavioral
// drift; see docs/testing.md.
void RunFixedFrames(int frames) {
    const float step = Host::sys_ticrate.value;
    for (int i = 0; i < frames; ++i) {
        Host::Host_Frame(step);
    }
    Host::Host_PrintStateHash();
    Common::Sys_Quit();
}

} // namespace

int main(int argc, char** argv) {
    std::signal(SIGFPE, SIG_IGN);

    quakeparms_t parms{};
    parms.memsize = kDefaultHeapSize;
    parms.membase = std::malloc(static_cast<size_t>(parms.memsize));
    parms.basedir = ".";
    parms.cachedir = nullptr;

    Common::COM_InitArgv(argc, argv);
    parms.argc = Common::com_argc;
    parms.argv = Common::com_argv;

    Common::Sys_Init();
    Host::Host_Init(&parms);
    Cvar::Register(&Host::sys_nostdout);

    if (int p = Common::COM_CheckParm("-runframes"); p && p < Common::com_argc - 1) {
        RunFixedFrames(Common::Q_atoi(Common::com_argv[p + 1]));
    }

    double oldtime = Common::Sys_FloatTime() - 0.1;
    while (true) {
        const double newtime = Common::Sys_FloatTime();
        double time = newtime - oldtime;

        if (Client::cls.state == ca_dedicated) {
            if (time < Host::sys_ticrate.value) {
                SDL_Delay(1);
                continue;
            }
            time = Host::sys_ticrate.value;
        }

        if (time > Host::sys_ticrate.value * 2) {
            oldtime = newtime;
        } else {
            oldtime += time;
        }

        Host::Host_Frame(static_cast<float>(time));
    }
}
