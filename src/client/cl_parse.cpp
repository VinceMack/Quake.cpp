// cl_parse.cpp -- Server Message Parsing and Network Packet Decoding Implementation
#include "client/cl_parse.hpp"
#include "client/cl_main.hpp"
#include "client/cl_tent.hpp"
#include "client/cl_demo.hpp"
#include "platform/crt_compat.hpp"
#include "render/software/sw_vid.hpp"
#include "render/software/sw_efrag.hpp"
#include "audio/audio_main.hpp"
#include "ui/screen.hpp"
#include "audio/audio_types.hpp"
#include "network/net_main.hpp"
#include "render/software/sw_part.hpp"
#include "quakedef.hpp"
#include "network/socket.hpp"
#include "ui/hud.hpp"
#include "core/filesystem.hpp"
#include "core/cmd.hpp"
#include "platform/system.hpp"
#include "render/software/sw_main.hpp"
#include "client/view.hpp"
#include "core/print.hpp"
#include "server/server_types.hpp"
#include "host/host.hpp"
#include "network/protocol.hpp"

namespace Client {

constexpr auto svc_strings = std::array { "svc_bad", "svc_nop", "svc_disconnect", "svc_updatestat", "svc_version",
    "svc_setview", "svc_sound", "svc_time", "svc_print", "svc_stufftext", "svc_setangle", "svc_serverinfo",
    "svc_lightstyle", "svc_updatename", "svc_updatefrags", "svc_clientdata", "svc_stopsound", "svc_updatecolors",
    "svc_particle", "svc_damage", "svc_spawnstatic", "OBSOLETE svc_spawnbinary", "svc_spawnbaseline", "svc_temp_entity",
    "svc_setpause", "svc_signonnum", "svc_centerprint", "svc_killedmonster", "svc_foundsecret", "svc_spawnstaticsound",
    "svc_intermission", "svc_finale", "svc_cdtrack", "svc_sellscreen", "svc_cutscene" };
static std::array<int, 16> bitcounts { };

void CL_ParseStartSoundPacket()
{
    int packet_vol = Audio::DEFAULT_SOUND_PACKET_VOLUME;
    float attenuation = Audio::DEFAULT_SOUND_PACKET_ATTENUATION;
    const int field_mask = Common::MSG_ReadByte();
    if (field_mask & SND_VOLUME) packet_vol = Common::MSG_ReadByte();
    if (field_mask & SND_ATTENUATION) attenuation = Common::MSG_ReadByte() / 64.0f;
    int channel = Common::MSG_ReadShort();
    const int sound_num = Common::MSG_ReadByte();
    const int ent = channel >> 3;
    channel &= 7;
    if (ent > MAX_EDICTS) Host::Host_Error("CL_ParseStartSoundPacket: ent = %i", ent);
    const Vector3 pos { Common::MSG_ReadCoord(), Common::MSG_ReadCoord(), Common::MSG_ReadCoord() };
    S_StartSound(ent, channel, cl.sound_precache[sound_num], pos, packet_vol / 255.0f, attenuation);
}

void CL_KeepaliveMessage()
{
    if (Server::sv.active || cls.demoplayback) return;
    sizebuf_t old = Net::net_message;
    std::array<byte, 8192> olddata;
    std::copy_n(
        Net::net_message.data, std::min(static_cast<int>(olddata.size()), Net::net_message.cursize), olddata.begin());
    int ret;
    do {
        ret = CL_GetMessage();
        switch (ret) {
        default:
            Host::Host_Error("CL_KeepaliveMessage: CL_GetMessage failed");
        case 0:
            break;
        case 1:
            Host::Host_Error("CL_KeepaliveMessage: received a message");
            break;
        case 2:
            if (Common::MSG_ReadByte() != svc_nop) Host::Host_Error("CL_KeepaliveMessage: datagram wasn't a nop");
            break;
        }
    } while (ret);
    Net::net_message = old;
    std::copy_n(
        olddata.begin(), std::min(static_cast<int>(olddata.size()), Net::net_message.cursize), Net::net_message.data);
    const float time = static_cast<float>(Common::Sys_FloatTime());
    static float lastmsg = 0.0f;
    if (time - lastmsg < 5.0f) return;
    lastmsg = time;
    Console::Con_Printf("--> client to server keepalive\n");
    Common::MSG_WriteByte(&cls.message, clc_nop);
    Net::NET_SendMessage(cls.netcon, &cls.message);
    Common::SZ_Clear(&cls.message);
}

void CL_ParseServerInfo()
{
    Console::Con_DPrintf("Serverinfo packet received.\n");
    CL_ClearState();
    if (Common::MSG_ReadLong() != PROTOCOL_VERSION) {
        Console::Con_Printf("Server version mismatch");
        return;
    }
    cl.maxclients = Common::MSG_ReadByte();
    if (cl.maxclients < 1 || cl.maxclients > MAX_SCOREBOARD) {
        Console::Con_Printf("Bad maxclients (%u)\n", cl.maxclients);
        return;
    }
    cl.scores_storage.assign(static_cast<size_t>(cl.maxclients), scoreboard_t { });
    cl.scores = cl.scores_storage.data();
    cl.gametype = Common::MSG_ReadByte();
    const char* str = Common::MSG_ReadString();
    strncpy_s(cl.levelname.data(), cl.levelname.size(), str, _TRUNCATE);
    Console::Con_Printf("\n\n\35\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36"
                        "\36\36\36\36\36\36\37\n\n%c%s\n",
        2, str);

    std::array<std::string, MAX_MODELS> model_names { };
    std::array<std::string, MAX_SOUNDS> sound_names { };
    cl.model_precache.fill(nullptr);
    cl.sound_precache.fill(nullptr);

    int nummodels = 1, numsounds = 1;
    while (char* mstr = Common::MSG_ReadString()) {
        if (!mstr[0]) break;
        if (nummodels < MAX_MODELS) model_names[nummodels++] = mstr;
    }
    while (char* sstr = Common::MSG_ReadString()) {
        if (!sstr[0]) break;
        if (numsounds < MAX_SOUNDS) sound_names[numsounds++] = sstr;
    }

    for (int idx = 1; idx < nummodels; ++idx) {
        cl.model_precache[idx] = Model::Mod_ForName(model_names[idx].c_str(), false);
        if (!cl.model_precache[idx]) {
            Console::Con_Printf("Model %s not found\n", model_names[idx].c_str());
            return;
        }
        CL_KeepaliveMessage();
    }
    Audio::S_BeginPrecaching();
    for (int idx = 1; idx < numsounds; ++idx) {
        cl.sound_precache[idx] = Audio::S_PrecacheSound(sound_names[idx].c_str());
        CL_KeepaliveMessage();
    }
    Audio::S_EndPrecaching();

    cl_entities[0].model = cl.worldmodel = cl.model_precache[1];
    Render::R_NewMap();
    Host::noclip_anglehack = false;
}

void CL_ParseUpdate(int bits)
{
    if (cls.signon == SIGNONS - 1) {
        cls.signon = SIGNONS;
        CL_SignonReply();
    }
    if (bits & U_MOREBITS) bits |= (Common::MSG_ReadByte() << 8);
    int num = (bits & U_LONGENTITY) ? Common::MSG_ReadShort() : Common::MSG_ReadByte();
    entity_t* ent = CL_EntityNum(num);
    for (int i = 0; i < 16; ++i) {
        if (bits & (1 << i)) bitcounts[i]++;
    }
    const bool forcelink = (ent->msgtime != cl.mtime[1]);
    ent->msgtime = cl.mtime[0];

    int modnum = (bits & U_MODEL) ? Common::MSG_ReadByte() : ent->baseline.modelindex;
    if (modnum >= MAX_MODELS) Host::Host_Error("CL_ParseModel: bad modnum");
    model_t* model = cl.model_precache[modnum];
    if (model != ent->model) {
        ent->model = model;
        if (model)
            ent->syncbase
                = (model->synctype == synctype_t::ST_RAND) ? (static_cast<float>(rand() & 0x7fff) / 0x7fff) : 0.0f;
    }
    ent->frame = (bits & U_FRAME) ? Common::MSG_ReadByte() : ent->baseline.frame;
    int colormap_idx = (bits & U_COLORMAP) ? Common::MSG_ReadByte() : ent->baseline.colormap;
    if (colormap_idx > cl.maxclients) Host::Host_Error("CL_ParseUpdate: colormap %i > maxclients", colormap_idx);
    ent->colormap = !colormap_idx ? Vid::vid.colormap : cl.scores[colormap_idx - 1].translations;
    ent->skinnum = (bits & U_SKIN) ? Common::MSG_ReadByte() : ent->baseline.skin;
    ent->effects = (bits & U_EFFECTS) ? Common::MSG_ReadByte() : ent->baseline.effects;
    ent->msg_origins[1] = ent->msg_origins[0];
    ent->msg_angles[1] = ent->msg_angles[0];

    ent->msg_origins[0][0] = (bits & U_ORIGIN1) ? Common::MSG_ReadCoord() : ent->baseline.origin[0];
    ent->msg_angles[0][0] = (bits & U_ANGLE1) ? Common::MSG_ReadAngle() : ent->baseline.angles[0];
    ent->msg_origins[0][1] = (bits & U_ORIGIN2) ? Common::MSG_ReadCoord() : ent->baseline.origin[1];
    ent->msg_angles[0][1] = (bits & U_ANGLE2) ? Common::MSG_ReadAngle() : ent->baseline.angles[1];
    ent->msg_origins[0][2] = (bits & U_ORIGIN3) ? Common::MSG_ReadCoord() : ent->baseline.origin[2];
    ent->msg_angles[0][2] = (bits & U_ANGLE3) ? Common::MSG_ReadAngle() : ent->baseline.angles[2];

    if (bits & U_NOLERP) ent->forcelink = true;
    if (forcelink || ent->forcelink) {
        ent->msg_origins[1] = ent->msg_origins[0];
        ent->origin = ent->msg_origins[0];
        ent->msg_angles[1] = ent->msg_angles[0];
        ent->angles = ent->msg_angles[0];
        ent->forcelink = true;
    }
}

void CL_ParseBaseline(entity_t* ent)
{
    ent->baseline.modelindex = Common::MSG_ReadByte();
    ent->baseline.frame = Common::MSG_ReadByte();
    ent->baseline.colormap = Common::MSG_ReadByte();
    ent->baseline.skin = Common::MSG_ReadByte();
    for (int i = 0; i < 3; ++i) {
        ent->baseline.origin[i] = Common::MSG_ReadCoord();
        ent->baseline.angles[i] = Common::MSG_ReadAngle();
    }
}

void CL_ParseClientdata(int bits)
{
    cl.viewheight
        = (bits & SU_VIEWHEIGHT) ? static_cast<float>(Common::MSG_ReadChar()) : static_cast<float>(DEFAULT_VIEWHEIGHT);
    cl.idealpitch = (bits & SU_IDEALPITCH) ? static_cast<float>(Common::MSG_ReadChar()) : 0.0f;
    cl.mvelocity[1] = cl.mvelocity[0];
    for (int i = 0; i < 3; ++i) {
        cl.punchangle[i] = (bits & (SU_PUNCH1 << i)) ? static_cast<float>(Common::MSG_ReadChar()) : 0.0f;
        cl.mvelocity[0][i] = (bits & (SU_VELOCITY1 << i)) ? static_cast<float>(Common::MSG_ReadChar() * 16) : 0.0f;
    }
    const int i = Common::MSG_ReadLong();
    if (cl.items != i) {
        Sbar::Sbar_Changed();
        for (int j = 0; j < 32; ++j) {
            if ((i & (1 << j)) && !(cl.items & (1 << j))) cl.item_gettime[j] = static_cast<float>(cl.time);
        }
        cl.items = i;
    }
    cl.onground = (bits & SU_ONGROUND) != 0;
    cl.inwater = (bits & SU_INWATER) != 0;
    cl.stats[STAT_WEAPONFRAME] = (bits & SU_WEAPONFRAME) ? Common::MSG_ReadByte() : 0;
    auto UpdateStat = [](int stat_idx, int new_val) {
        if (cl.stats[stat_idx] != new_val) {
            cl.stats[stat_idx] = new_val;
            Sbar::Sbar_Changed();
        }
    };
    UpdateStat(STAT_ARMOR, (bits & SU_ARMOR) ? Common::MSG_ReadByte() : 0);
    UpdateStat(STAT_WEAPON, (bits & SU_WEAPON) ? Common::MSG_ReadByte() : 0);
    UpdateStat(STAT_HEALTH, Common::MSG_ReadShort());
    UpdateStat(STAT_AMMO, Common::MSG_ReadByte());
    for (int idx = 0; idx < 4; ++idx) UpdateStat(STAT_SHELLS + idx, Common::MSG_ReadByte());
    const int active_weapon_val = Common::MSG_ReadByte();
    UpdateStat(STAT_ACTIVEWEAPON, Common::standard_quake ? active_weapon_val : (1 << active_weapon_val));
}

void CL_NewTranslation(int slot)
{
    if (slot > cl.maxclients) Common::Sys_Error("CL_NewTranslation: slot > cl.maxclients");
    byte* dest = cl.scores[slot].translations.data();
    const byte* source = Vid::vid.colormap;
    std::copy_n(Vid::vid.colormap, cl.scores[slot].translations.size(), dest);
    const int top = cl.scores[slot].colors & 0xf0, bottom = (cl.scores[slot].colors & 15) << 4;
    for (int i = 0; i < VID_GRADES; ++i, dest += 256, source += 256) {
        if (top < 128)
            std::copy_n(source + top, 16, dest + TOP_RANGE);
        else
            for (int j = 0; j < 16; ++j) dest[TOP_RANGE + j] = source[top + 15 - j];
        if (bottom < 128)
            std::copy_n(source + bottom, 16, dest + BOTTOM_RANGE);
        else
            for (int j = 0; j < 16; ++j) dest[BOTTOM_RANGE + j] = source[bottom + 15 - j];
    }
}

void CL_ParseStatic()
{
    if (cl.num_statics >= MAX_STATIC_ENTITIES) Host::Host_Error("Too many static entities");
    entity_t* ent = &cl_static_entities[cl.num_statics++];
    CL_ParseBaseline(ent);
    ent->model = cl.model_precache[ent->baseline.modelindex];
    ent->frame = ent->baseline.frame;
    ent->colormap = Vid::vid.colormap;
    ent->skinnum = ent->baseline.skin;
    ent->effects = ent->baseline.effects;
    ent->origin = ent->baseline.origin;
    ent->angles = ent->baseline.angles;
    Render::R_AddEfrags(ent);
}

void CL_ParseStaticSound()
{
    const Vector3 org { Common::MSG_ReadCoord(), Common::MSG_ReadCoord(), Common::MSG_ReadCoord() };
    const int sound_num = Common::MSG_ReadByte(), vol = Common::MSG_ReadByte(), atten = Common::MSG_ReadByte();
    S_StaticSound(cl.sound_precache[sound_num], org, static_cast<float>(vol), static_cast<float>(atten));
}

#define SHOWNET(x)                                                                                                     \
    if (cl_shownet.value == 2) Console::Con_Printf("%3i:%s\n", Common::msg_readcount - 1, x);

void CL_ParseServerMessage()
{
    if (cl_shownet.value == 1)
        Console::Con_Printf("%i ", Net::net_message.cursize);
    else if (cl_shownet.value == 2)
        Console::Con_Printf("------------------\n");
    cl.onground = false;
    Common::MSG_BeginReading();

    while (true) {
        if (Common::msg_badread) Host::Host_Error("CL_ParseServerMessage: Bad server message");
        const int cmd = Common::MSG_ReadByte();
        if (cmd == -1) {
            SHOWNET("END OF MESSAGE");
            return;
        }
        if (cmd & 128) {
            SHOWNET("fast update");
            CL_ParseUpdate(cmd & 127);
            continue;
        }
        SHOWNET(svc_strings[cmd]);

        switch (cmd) {
        default:
            Host::Host_Error("CL_ParseServerMessage: Illegible server message\n");
            break;
        case svc_nop:
            break;
        case svc_time:
            cl.mtime[1] = cl.mtime[0];
            cl.mtime[0] = Common::MSG_ReadFloat();
            break;
        case svc_clientdata:
            CL_ParseClientdata(Common::MSG_ReadShort());
            break;
        case svc_version:
            if (Common::MSG_ReadLong() != PROTOCOL_VERSION)
                Host::Host_Error("CL_ParseServerMessage: Server version mismatch\n");
            break;
        case svc_disconnect:
            Host::Host_EndGame("Server disconnected\n");
            break;
        case svc_print:
            Console::Con_Printf("%s", Common::MSG_ReadString());
            break;
        case svc_centerprint:
            Screen::GetScreenSystem().CenterPrint(Common::MSG_ReadString());
            break;
        case svc_stufftext:
            Cmd::BufferAddText(Common::MSG_ReadString());
            break;
        case svc_damage:
            View::V_ParseDamage();
            break;
        case svc_serverinfo:
            CL_ParseServerInfo();
            Vid::vid.recalc_refdef = true;
            break;
        case svc_setangle:
            for (int i = 0; i < 3; ++i) cl.viewangles[i] = Common::MSG_ReadAngle();
            break;
        case svc_setview:
            cl.viewentity = Common::MSG_ReadShort();
            break;
        case svc_lightstyle: {
            const int i = Common::MSG_ReadByte();
            if (i >= MAX_LIGHTSTYLES) Common::Sys_Error("svc_lightstyle > MAX_LIGHTSTYLES");
            Common::Q_strcpy(cl_lightstyle[i].map.data(), Common::MSG_ReadString());
            cl_lightstyle[i].length = Common::Q_strlen(cl_lightstyle[i].map.data());
            break;
        }
        case svc_sound:
            CL_ParseStartSoundPacket();
            break;
        case svc_stopsound: {
            const int i = Common::MSG_ReadShort();
            Audio::S_StopSound(i >> 3, i & 7);
            break;
        }
        case svc_updatename: {
            Sbar::Sbar_Changed();
            const int i = Common::MSG_ReadByte();
            if (i >= cl.maxclients) Host::Host_Error("CL_ParseServerMessage: svc_updatename > MAX_SCOREBOARD");
            strcpy_s(cl.scores[i].name.data(), cl.scores[i].name.size(), Common::MSG_ReadString());
            break;
        }
        case svc_updatefrags: {
            Sbar::Sbar_Changed();
            const int i = Common::MSG_ReadByte();
            if (i >= cl.maxclients) Host::Host_Error("CL_ParseServerMessage: svc_updatefrags > MAX_SCOREBOARD");
            cl.scores[i].frags = Common::MSG_ReadShort();
            break;
        }
        case svc_updatecolors: {
            Sbar::Sbar_Changed();
            const int i = Common::MSG_ReadByte();
            if (i >= cl.maxclients) Host::Host_Error("CL_ParseServerMessage: svc_updatecolors > MAX_SCOREBOARD");
            cl.scores[i].colors = Common::MSG_ReadByte();
            CL_NewTranslation(i);
            break;
        }
        case svc_particle:
            Render::R_ParseParticleEffect();
            break;
        case svc_spawnbaseline:
            CL_ParseBaseline(CL_EntityNum(Common::MSG_ReadShort()));
            break;
        case svc_spawnstatic:
            CL_ParseStatic();
            break;
        case svc_temp_entity:
            CL_ParseTEnt();
            break;
        case svc_setpause:
            cl.paused = Common::MSG_ReadByte();
            Vid::VID_HandlePause();
            break;
        case svc_signonnum: {
            const int i = Common::MSG_ReadByte();
            if (i <= cls.signon) Host::Host_Error("Received signon %i when at %i", i, cls.signon);
            cls.signon = i;
            CL_SignonReply();
            break;
        }
        case svc_killedmonster:
            cl.stats[STAT_MONSTERS]++;
            break;
        case svc_foundsecret:
            cl.stats[STAT_SECRETS]++;
            break;
        case svc_updatestat: {
            const int i = Common::MSG_ReadByte();
            if (i < 0 || i >= MAX_CL_STATS) Common::Sys_Error("svc_updatestat: %i is invalid", i);
            cl.stats[i] = Common::MSG_ReadLong();
            break;
        }
        case svc_spawnstaticsound:
            CL_ParseStaticSound();
            break;
        case svc_cdtrack:
            cl.cdtrack = Common::MSG_ReadByte();
            cl.looptrack = Common::MSG_ReadByte();
            break;
        case svc_intermission:
        case svc_finale:
        case svc_cutscene:
            cl.intermission = (cmd == svc_intermission) ? 1 : ((cmd == svc_finale) ? 2 : 3);
            cl.completed_time = static_cast<int>(cl.time);
            Vid::vid.recalc_refdef = true;
            if (cmd != svc_intermission) Screen::GetScreenSystem().CenterPrint(Common::MSG_ReadString());
            break;
        case svc_sellscreen:
            Cmd::ExecuteString("help", Cmd::Source::Command);
            break;
        }
    }
}

} // namespace Client
