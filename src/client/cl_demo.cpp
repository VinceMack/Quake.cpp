// cl_demo.cpp -- Client Demo Recording and Playback Subsystem Implementation
#include "quakedef.hpp"
#include "client/cl_demo.hpp"
#include "client/cl_main.hpp"

#ifdef GetMessage
#undef GetMessage
#endif

using namespace Common;
using namespace Console;
using namespace Cmd;
using namespace Host;
using namespace Net;
using namespace Screen;

namespace Client {

void CL_FinishTimeDemo() {
    cls.timedemo = false;
    const int frames = (host_framecount - cls.td_startframe) - 1;
    float time = static_cast<float>(realtime - cls.td_starttime);
    if (!time) time = 1.0f;
    Con_Printf("%i frames %5.1f seconds %5.1f fps\n", frames, time, frames / time);
}

void CL_NextDemo() {
    if (cls.demonum == -1) return;
    Screen::GetScreenSystem().BeginLoadingPlaque();
    if (!cls.demos[cls.demonum][0] || cls.demonum == MAX_DEMOS) {
        cls.demonum = 0;
        if (!cls.demos[0][0]) {
            Con_Printf("No demos listed with startdemos\n");
            cls.demonum = -1;
            return;
        }
    }
    Cmd::BufferInsertText(va("playdemo %s\n", cls.demos[cls.demonum++].data()));
}

void CL_StopPlayback() {
    if (!cls.demoplayback) return;
    fclose(cls.demofile);
    cls.demoplayback = false;
    cls.demofile = nullptr;
    cls.state = ca_disconnected;
    if (cls.timedemo) CL_FinishTimeDemo();
    else CL_NextDemo();
}

void CL_WriteDemoMessage() {
    int len = LittleLong(net_message.cursize);
    fwrite(&len, 4, 1, cls.demofile);
    for (int i = 0; i < 3; ++i) {
        float f = LittleFloat(cl.viewangles[i]);
        fwrite(&f, 4, 1, cls.demofile);
    }
    fwrite(net_message.data, net_message.cursize, 1, cls.demofile);
    fflush(cls.demofile);
}

int CL_GetMessage() {
    if (cls.demoplayback) {
        if (cls.signon == SIGNONS) {
            if (cls.timedemo) {
                if (host_framecount == cls.td_lastframe) return 0;
                cls.td_lastframe = host_framecount;
                if (host_framecount == cls.td_startframe + 1)
                    cls.td_starttime = static_cast<float>(realtime);
            } else if (cl.time <= cl.mtime[0]) return 0;
        }
        fread(&net_message.cursize, 4, 1, cls.demofile);
        cl.mviewangles[1] = cl.mviewangles[0];
        for (int i = 0; i < 3; ++i) {
            float f = 0.0f;
            fread(&f, 4, 1, cls.demofile);
            cl.mviewangles[0][i] = LittleFloat(f);
        }
        net_message.cursize = LittleLong(net_message.cursize);
        if (net_message.cursize > MAX_MSGLEN) Sys_Error("Demo message > MAX_MSGLEN");
        if (fread(net_message.data, net_message.cursize, 1, cls.demofile) != 1) {
            CL_StopPlayback();
            return 0;
        }
        return 1;
    }
    while (true) {
        const int r = NET_GetMessage(cls.netcon);
        if (r != 1 && r != 2) return r;
        if (net_message.cursize == 1 && net_message.data[0] == svc_nop) {
            Con_Printf("<-- server to client keepalive\n");
        } else {
            return r;
        }
    }
}

void CL_Stop_f() {
    if (Cmd::state.source != Cmd::Source::Command) return;
    if (!cls.demorecording) {
        Con_Printf("Not recording a demo.\n");
        return;
    }
    SZ_Clear(&net_message);
    MSG_WriteByte(&net_message, svc_disconnect);
    CL_WriteDemoMessage();
    fclose(cls.demofile);
    cls.demofile = nullptr;
    cls.demorecording = false;
    Con_Printf("Completed demo\n");
}

void CL_Record_f() {
    if (Cmd::state.source != Cmd::Source::Command) return;
    const int c = Cmd::Argc();
    if (c < 2 || c > 4) {
        Con_Printf("record <demoname> [<map> [cd track]]\n");
        return;
    }
    if (Cmd::Argv(1).find("..") != eastl::string_view::npos) {
        Con_Printf("Relative pathnames are not allowed.\n");
        return;
    }
    if (c == 2 && cls.state == ca_connected) {
        Con_Printf("Can not record - already connected to server\nClient demo recording must be started before connecting\n");
        return;
    }
    int track = (c == 4) ? Q_atoi(Cmd::Argv(3)) : -1;
    if (c == 4) Con_Printf("Forcing CD track to %i\n", cls.forcetrack);
    if (c > 2) Cmd::ExecuteString(("map " + eastl::string(Cmd::Argv(2))).c_str(), Cmd::Source::Command);
    char name_buffer[MAX_OSPATH];
    strcpy_s(name_buffer, sizeof(name_buffer), (eastl::string(com_gamedir) + "/" + eastl::string(Cmd::Argv(1))).c_str());
    COM_DefaultExtension(name_buffer, ".dem");
    Con_Printf("recording to %s.\n", name_buffer);
    fopen_s(&cls.demofile, name_buffer, "wb");
    if (!cls.demofile) {
        Con_Printf("ERROR: couldn't open.\n");
        return;
    }
    cls.forcetrack = track;
    fprintf(cls.demofile, "%i\n", cls.forcetrack);
    cls.demorecording = true;
}

void CL_PlayDemo_f() {
    if (Cmd::state.source != Cmd::Source::Command) return;
    if (Cmd::Argc() != 2) {
        Con_Printf("play <demoname> : plays a demo\n");
        return;
    }
    CL_Disconnect();
    char name[256];
    strcpy_s(name, sizeof(name), eastl::string(Cmd::Argv(1)).c_str());
    COM_DefaultExtension(name, ".dem");
    Con_Printf("Playing demo from %s.\n", name);
    COM_FOpenFile(name, &cls.demofile);
    if (!cls.demofile) {
        Con_Printf("ERROR: couldn't open.\n");
        cls.demonum = -1;
        return;
    }
    cls.demoplayback = true;
    cls.state = ca_connected;
    cls.forcetrack = 0;
    int c;
    bool neg = false;
    while ((c = getc(cls.demofile)) != '\n') {
        if (c == '-') neg = true;
        else cls.forcetrack = cls.forcetrack * 10 + (c - '0');
    }
    if (neg) cls.forcetrack = -cls.forcetrack;
}

void CL_TimeDemo_f() {
    if (Cmd::state.source != Cmd::Source::Command || Cmd::Argc() != 2) {
        Con_Printf("timedemo <demoname> : gets demo speeds\n");
        return;
    }
    CL_PlayDemo_f();
    cls.timedemo = true;
    cls.td_startframe = host_framecount;
    cls.td_lastframe = -1;
}

} // namespace Client
