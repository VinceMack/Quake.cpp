// cl_input.cpp -- Client Movement Input and Command Generation Implementation
#include "quakedef.hpp"
#include "client/cl_input.hpp"
#include "client/cl_main.hpp"

using namespace Common;
using namespace Console;
using namespace Cvar;
using namespace Cmd;
using namespace Host;
using namespace View;
using namespace Math;
using namespace Net;

namespace Client {

kbutton_t in_mlook, in_klook, in_left, in_right, in_forward, in_back;
kbutton_t in_lookup, in_lookdown, in_moveleft, in_moveright;
kbutton_t in_strafe, in_speed, in_use, in_jump, in_attack, in_up, in_down;
int in_impulse = 0;

static void KeyDown(kbutton_t* b) {
    std::string_view c = Cmd::Argv(1);
    int k = c.empty() ? -1 : Q_atoi(c);
    if (k == b->down[0] || k == b->down[1]) return;
    if (!b->down[0]) b->down[0] = k;
    else if (!b->down[1]) b->down[1] = k;
    else { Con_Printf("Three keys down for a button!\n"); return; }
    if (!(b->state & 1)) b->state |= 3;
}

static void KeyUp(kbutton_t* b) {
    std::string_view c = Cmd::Argv(1);
    if (c.empty()) { b->down[0] = b->down[1] = 0; b->state = 4; return; }
    int k = Q_atoi(c);
    if (b->down[0] == k) b->down[0] = 0;
    else if (b->down[1] == k) b->down[1] = 0;
    else return;
    if (!b->down[0] && !b->down[1] && (b->state & 1)) { b->state &= ~1; b->state |= 4; }
}

float CL_KeyState(kbutton_t* key) {
    float val = 0.0f;
    const bool idown = (key->state & 2) != 0, iup = (key->state & 4) != 0, down = (key->state & 1) != 0;
    if (idown && !iup && down) val = 0.5f;
    if (iup && !idown && !down) val = 0.0f;
    if (!idown && !iup && down) val = 1.0f;
    if (idown && iup) val = down ? 0.75f : 0.25f;
    key->state &= 1;
    return val;
}

void CL_AdjustAngles() {
    float speed = static_cast<float>((in_speed.state & 1) ? host_frametime * cl_anglespeedkey.value : host_frametime);
    if (!(in_strafe.state & 1)) {
        cl.viewangles[YAW] = anglemod(cl.viewangles[YAW] + speed * cl_yawspeed.value * (CL_KeyState(&in_left) - CL_KeyState(&in_right)));
    }
    if (in_klook.state & 1) {
        V_StopPitchDrift();
        cl.viewangles[PITCH] += speed * cl_pitchspeed.value * (CL_KeyState(&in_back) - CL_KeyState(&in_forward));
    }
    const float up = CL_KeyState(&in_lookup), down = CL_KeyState(&in_lookdown);
    cl.viewangles[PITCH] += speed * cl_pitchspeed.value * (down - up);
    if (up || down) V_StopPitchDrift();
    cl.viewangles[PITCH] = std::clamp(cl.viewangles[PITCH], -70.0f, 80.0f);
    cl.viewangles[ROLL]  = std::clamp(cl.viewangles[ROLL], -50.0f, 50.0f);
}

void CL_BaseMove(usercmd_t* cmd) {
    if (cls.signon != SIGNONS) return;
    CL_AdjustAngles();
    *cmd = {};
    if (in_strafe.state & 1) cmd->sidemove += cl_sidespeed.value * (CL_KeyState(&in_right) - CL_KeyState(&in_left));
    cmd->sidemove += cl_sidespeed.value * (CL_KeyState(&in_moveright) - CL_KeyState(&in_moveleft));
    cmd->upmove   += cl_upspeed.value * (CL_KeyState(&in_up) - CL_KeyState(&in_down));
    if (!(in_klook.state & 1)) {
        cmd->forwardmove += cl_forwardspeed.value * CL_KeyState(&in_forward) - cl_backspeed.value * CL_KeyState(&in_back);
    }
    if (in_speed.state & 1) {
        cmd->forwardmove *= cl_movespeedkey.value;
        cmd->sidemove *= cl_movespeedkey.value;
        cmd->upmove *= cl_movespeedkey.value;
    }
}

void CL_SendMove(usercmd_t* cmd) {
    std::array<byte, 128> data{};
    sizebuf_t buf{};
    buf.data = data.data();
    buf.maxsize = 128;
    buf.cursize = 0;
    cl.cmd = *cmd;

    MSG_WriteByte(&buf, clc_move);
    MSG_WriteFloat(&buf, static_cast<float>(cl.mtime[0]));
    for (int i = 0; i < 3; ++i) MSG_WriteAngle(&buf, cl.viewangles[i]);
    MSG_WriteShort(&buf, static_cast<int>(cmd->forwardmove));
    MSG_WriteShort(&buf, static_cast<int>(cmd->sidemove));
    MSG_WriteShort(&buf, static_cast<int>(cmd->upmove));
    int bits = (in_attack.state & 3 ? 1 : 0) | (in_jump.state & 3 ? 2 : 0);
    in_attack.state &= ~2;
    in_jump.state &= ~2;
    MSG_WriteByte(&buf, bits);
    MSG_WriteByte(&buf, in_impulse);
    in_impulse = 0;

    if (cls.demoplayback || ++cl.movemessages <= 2) return;
    if (NET_SendUnreliableMessage(cls.netcon, &buf) == -1) {
        Con_Printf("CL_SendMove: lost server connection\n");
        CL_Disconnect();
    }
}

struct BtnPair { const char* name; kbutton_t* btn; };

void CL_InitInput() {
    auto BindBtn = [](const char* name, kbutton_t* btn) {
        Cmd::AddCommand(("+" + std::string(name)).c_str(), [btn]() { KeyDown(btn); });
        Cmd::AddCommand(("-" + std::string(name)).c_str(), [btn]() { KeyUp(btn); });
    };
    constexpr BtnPair btns[] = {
        {"moveup", &in_up}, {"movedown", &in_down}, {"left", &in_left}, {"right", &in_right},
        {"forward", &in_forward}, {"back", &in_back}, {"lookup", &in_lookup}, {"lookdown", &in_lookdown},
        {"strafe", &in_strafe}, {"moveleft", &in_moveleft}, {"moveright", &in_moveright},
        {"speed", &in_speed}, {"attack", &in_attack}, {"use", &in_use}, {"jump", &in_jump}, {"klook", &in_klook}
    };
    for (auto [name, btn] : btns) BindBtn(name, btn);
    Cmd::AddCommand("impulse", []() { in_impulse = Q_atoi(Cmd::Argv(1)); });
    Cmd::AddCommand("+mlook", []() { KeyDown(&in_mlook); });
    Cmd::AddCommand("-mlook", []() { KeyUp(&in_mlook); if (!(in_mlook.state & 1) && lookspring.value) V_StartPitchDrift(); });
}

} // namespace Client
