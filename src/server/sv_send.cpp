// sv_send.cpp -- Server Network Transmission, Client Messaging, and Client Prediction/Think Implementation
#include "server/sv_send.hpp"
#include "server/server.hpp"
#include "server/world.hpp"
#include "server/physics.hpp"
#include "platform/crt_compat.hpp"
#include "host/host.hpp"
#include "platform/system.hpp"
#include "core/filesystem.hpp"
#include "client/input.hpp"
#include "network/net_main.hpp"
#include "network/protocol.hpp"
#include "quakedef.hpp"
#include "network/socket.hpp"
#include "vm/interpreter.hpp"
#include "client/view.hpp"
#include "core/print.hpp"
#include "audio/audio_types.hpp"
#include "core/cmd.hpp"

namespace Server {

static int fatbytes = 0;
static std::array<byte, MAX_MAP_LEAFS / 8> fatpvs { };

edict_t* sv_player = nullptr;
static Vector3 wishdir { };
static float wishspeed = 0.0f;
static float* angles = nullptr;
static float* origin = nullptr;
static float* velocity = nullptr;
static qboolean onground = false;
static usercmd_t cmd { };

void SV_StartParticle(const Vector3& org, const Vector3& dir, int color, int count)
{
    if (sv.datagram.cursize > MAX_DATAGRAM - 16) return;

    Common::MSG_WriteByte(&sv.datagram, svc_particle);
    Common::MSG_WriteCoord(&sv.datagram, org[0]);
    Common::MSG_WriteCoord(&sv.datagram, org[1]);
    Common::MSG_WriteCoord(&sv.datagram, org[2]);
    for (int i = 0; i < 3; ++i) {
        int v = static_cast<int>(dir[i] * 16.0f);
        if (v > 127)
            v = 127;
        else if (v < -128)
            v = -128;
        Common::MSG_WriteChar(&sv.datagram, v);
    }
    Common::MSG_WriteByte(&sv.datagram, count);
    Common::MSG_WriteByte(&sv.datagram, color);
}

void SV_StartSound(edict_t* entity, int channel, const char* sample, int vol, float attenuation)
{
    if (vol < 0 || vol > 255) Common::Sys_Error("SV_StartSound: volume = %i", vol);
    if (attenuation < 0.0f || attenuation > 4.0f)
        Common::Sys_Error("SV_StartSound: attenuation = %f", static_cast<double>(attenuation));
    if (channel < 0 || channel > 7) Common::Sys_Error("SV_StartSound: channel = %i", channel);
    if (sv.datagram.cursize > MAX_DATAGRAM - 16) return;

    int sound_num = 1;
    for (; sound_num < MAX_SOUNDS && sv.sound_precache[sound_num]; ++sound_num) {
        if (!strcmp(sample, sv.sound_precache[sound_num])) break;
    }

    if (sound_num == MAX_SOUNDS || !sv.sound_precache[sound_num]) {
        Console::Con_Printf("SV_StartSound: %s not precacheed\n", sample);
        return;
    }

    const int ent = VM::NUM_FOR_EDICT(entity);
    channel = (ent << 3) | channel;

    int field_mask = 0;
    if (vol != Audio::DEFAULT_SOUND_PACKET_VOLUME) field_mask |= SND_VOLUME;
    if (attenuation != Audio::DEFAULT_SOUND_PACKET_ATTENUATION) field_mask |= SND_ATTENUATION;

    Common::MSG_WriteByte(&sv.datagram, svc_sound);
    Common::MSG_WriteByte(&sv.datagram, field_mask);
    if (field_mask & SND_VOLUME) Common::MSG_WriteByte(&sv.datagram, vol);
    if (field_mask & SND_ATTENUATION) Common::MSG_WriteByte(&sv.datagram, static_cast<int>(attenuation * 64.0f));

    Common::MSG_WriteShort(&sv.datagram, channel);
    Common::MSG_WriteByte(&sv.datagram, sound_num);
    const Vector3 center = entity->v.origin + (entity->v.mins + entity->v.maxs) * 0.5f;
    for (int i = 0; i < 3; ++i) Common::MSG_WriteCoord(&sv.datagram, center[i]);
}

void SV_SendServerinfo(client_t* client)
{
    std::array<char, 2048> message { };

    Common::MSG_WriteByte(&client->message, svc_print);
    sprintf_s(message.data(), message.size(), "%c\nVERSION %4.2f SERVER (%i CRC)", 2, static_cast<double>(VERSION),
        VM::pr_crc);
    Common::MSG_WriteString(&client->message, message.data());

    Common::MSG_WriteByte(&client->message, svc_serverinfo);
    Common::MSG_WriteLong(&client->message, PROTOCOL_VERSION);
    Common::MSG_WriteByte(&client->message, svs.maxclients);

    if (!coop.value && deathmatch.value)
        Common::MSG_WriteByte(&client->message, GAME_DEATHMATCH);
    else
        Common::MSG_WriteByte(&client->message, GAME_COOP);

    sprintf_s(message.data(), message.size(), "%s", VM::PR_GetString(sv.edicts->v.message));
    Common::MSG_WriteString(&client->message, message.data());

    for (size_t i = 1; i < sv.model_precache.size() && sv.model_precache[i]; ++i) {
        Common::MSG_WriteString(&client->message, sv.model_precache[i]);
    }
    Common::MSG_WriteByte(&client->message, 0);

    for (size_t i = 1; i < sv.sound_precache.size() && sv.sound_precache[i]; ++i) {
        Common::MSG_WriteString(&client->message, sv.sound_precache[i]);
    }
    Common::MSG_WriteByte(&client->message, 0);

    Common::MSG_WriteByte(&client->message, svc_cdtrack);
    Common::MSG_WriteByte(&client->message, static_cast<int>(sv.edicts->v.sounds));
    Common::MSG_WriteByte(&client->message, static_cast<int>(sv.edicts->v.sounds));

    Common::MSG_WriteByte(&client->message, svc_setview);
    Common::MSG_WriteShort(&client->message, VM::NUM_FOR_EDICT(client->edict));

    Common::MSG_WriteByte(&client->message, svc_signonnum);
    Common::MSG_WriteByte(&client->message, 1);

    client->sendsignon = true;
    client->spawned = false;
}

void SV_ConnectClient(int clientnum)
{
    client_t* client = &svs.GetClients()[static_cast<size_t>(clientnum)];
    Console::Con_DPrintf("Client %s connected\n", client->netconnection->address);

    const int edictnum = clientnum + 1;
    edict_t* ent = VM::EDICT_NUM(edictnum);
    qsocket_s* netconnection = client->netconnection;

    std::array<float, NUM_SPAWN_PARMS> spawn_parms { };
    if (sv.loadgame) {
        std::copy(client->spawn_parms.begin(), client->spawn_parms.end(), spawn_parms.begin());
    }

    client->Reset();
    client->netconnection = netconnection;
    client->SetName("unconnected");
    client->active = true;
    client->spawned = false;
    client->edict = ent;
    client->message.data = client->msgbuf.data();
    client->message.maxsize = static_cast<int>(client->msgbuf.size());
    client->message.allowoverflow = true;
    client->privileged = false;

    if (sv.loadgame) {
        std::copy(spawn_parms.begin(), spawn_parms.end(), client->spawn_parms.begin());
    } else {
        VM::PR_ExecuteProgram(VM::pr_global_struct->SetNewParms);
        for (int i = 0; i < NUM_SPAWN_PARMS; ++i) {
            client->spawn_parms[static_cast<size_t>(i)] = (&VM::pr_global_struct->parm1)[i];
        }
    }

    SV_SendServerinfo(client);
}

void SV_CheckForNewClients()
{
    while (true) {
        qsocket_s* ret = Net::NET_CheckNewConnections();
        if (!ret) break;

        auto clients = svs.GetClients();
        auto it = std::find_if(clients.begin(), clients.end(), [](const client_t& cl) { return !cl.active; });

        if (it == clients.end()) Common::Sys_Error("Host_CheckForNewClients: no free clients");

        const int i = static_cast<int>(std::distance(clients.begin(), it));
        it->netconnection = ret;
        SV_ConnectClient(i);

        Net::net_activeconnections++;
    }
}

void SV_AddToFatPVS(const Vector3& org, mnode_t* node)
{
    while (true) {
        if (node->contents < 0) {
            if (node->contents != CONTENTS_SOLID) {
                const byte* pvs = Model::Mod_LeafPVS(reinterpret_cast<mleaf_t*>(node), sv.worldmodel);
                for (int i = 0; i < fatbytes; ++i) {
                    fatpvs[static_cast<size_t>(i)] |= pvs[i];
                }
            }
            return;
        }

        mplane_t* plane = node->plane;
        const float d = org.dot(plane->normal) - plane->dist;
        if (d > 8.0f)
            node = node->children[0];
        else if (d < -8.0f)
            node = node->children[1];
        else {
            SV_AddToFatPVS(org, node->children[0]);
            node = node->children[1];
        }
    }
}

byte* SV_FatPVS(const Vector3& org)
{
    fatbytes = (sv.worldmodel->numleafs + 31) >> 3;
    std::fill_n(fatpvs.begin(), static_cast<size_t>(fatbytes), static_cast<byte>(0));
    SV_AddToFatPVS(org, sv.worldmodel->nodes);
    return fatpvs.data();
}

void SV_WriteEntitiesToClient(edict_t* clent, sizebuf_t* msg)
{
    const Vector3 org = clent->v.origin + clent->v.view_ofs;
    const byte* pvs = SV_FatPVS(org);

    edict_t* ent = NEXT_EDICT(sv.edicts);
    for (int e = 1; e < sv.num_edicts; ++e, ent = NEXT_EDICT(ent)) {
        if (ent != clent) {
            if (!ent->v.modelindex || !*VM::PR_GetString(ent->v.model)) continue;

            int i = 0;
            for (; i < ent->num_leafs; ++i) {
                if (pvs[ent->leafnums[i] >> 3] & (1 << (ent->leafnums[i] & 7))) break;
            }
            if (i == ent->num_leafs) continue;
        }

        if (msg->maxsize - msg->cursize < 16) {
            Console::Con_Printf("packet overflow\n");
            return;
        }

        int bits = 0;
        for (int i = 0; i < 3; ++i) {
            const float miss = ent->v.origin[i] - ent->baseline.origin[i];
            if (miss < -0.1f || miss > 0.1f) bits |= U_ORIGIN1 << i;
        }

        if (ent->v.angles[0] != ent->baseline.angles[0]) bits |= U_ANGLE1;
        if (ent->v.angles[1] != ent->baseline.angles[1]) bits |= U_ANGLE2;
        if (ent->v.angles[2] != ent->baseline.angles[2]) bits |= U_ANGLE3;

        if (ent->v.movetype == MOVETYPE_STEP) bits |= U_NOLERP;
        if (ent->baseline.colormap != ent->v.colormap) bits |= U_COLORMAP;
        if (ent->baseline.skin != ent->v.skin) bits |= U_SKIN;
        if (ent->baseline.frame != ent->v.frame) bits |= U_FRAME;
        if (ent->baseline.effects != ent->v.effects) bits |= U_EFFECTS;
        if (ent->baseline.modelindex != ent->v.modelindex) bits |= U_MODEL;
        if (e >= 256) bits |= U_LONGENTITY;
        if (bits >= 256) bits |= U_MOREBITS;

        Common::MSG_WriteByte(msg, bits | U_SIGNAL);
        if (bits & U_MOREBITS) Common::MSG_WriteByte(msg, bits >> 8);
        if (bits & U_LONGENTITY)
            Common::MSG_WriteShort(msg, e);
        else
            Common::MSG_WriteByte(msg, e);

        if (bits & U_MODEL) Common::MSG_WriteByte(msg, static_cast<int>(ent->v.modelindex));
        if (bits & U_FRAME) Common::MSG_WriteByte(msg, static_cast<int>(ent->v.frame));
        if (bits & U_COLORMAP) Common::MSG_WriteByte(msg, static_cast<int>(ent->v.colormap));
        if (bits & U_SKIN) Common::MSG_WriteByte(msg, static_cast<int>(ent->v.skin));
        if (bits & U_EFFECTS) Common::MSG_WriteByte(msg, static_cast<int>(ent->v.effects));
        if (bits & U_ORIGIN1) Common::MSG_WriteCoord(msg, ent->v.origin[0]);
        if (bits & U_ANGLE1) Common::MSG_WriteAngle(msg, ent->v.angles[0]);
        if (bits & U_ORIGIN2) Common::MSG_WriteCoord(msg, ent->v.origin[1]);
        if (bits & U_ANGLE2) Common::MSG_WriteAngle(msg, ent->v.angles[1]);
        if (bits & U_ORIGIN3) Common::MSG_WriteCoord(msg, ent->v.origin[2]);
        if (bits & U_ANGLE3) Common::MSG_WriteAngle(msg, ent->v.angles[2]);
    }
}

void SV_CleanupEnts()
{
    edict_t* ent = NEXT_EDICT(sv.edicts);
    for (int e = 1; e < sv.num_edicts; ++e, ent = NEXT_EDICT(ent)) {
        ent->v.effects = static_cast<float>(static_cast<int>(ent->v.effects) & ~EF_MUZZLEFLASH);
    }
}

void SV_WriteClientdataToMessage(edict_t* ent, sizebuf_t* msg)
{
    if (ent->v.dmg_take || ent->v.dmg_save) {
        const edict_t* other = PROG_TO_EDICT(ent->v.dmg_inflictor);
        Common::MSG_WriteByte(msg, svc_damage);
        Common::MSG_WriteByte(msg, static_cast<int>(ent->v.dmg_save));
        Common::MSG_WriteByte(msg, static_cast<int>(ent->v.dmg_take));
        const Vector3 center = other->v.origin + (other->v.mins + other->v.maxs) * 0.5f;
        for (int i = 0; i < 3; ++i) Common::MSG_WriteCoord(msg, center[i]);
        ent->v.dmg_take = 0;
        ent->v.dmg_save = 0;
    }

    SV_SetIdealPitch();

    if (ent->v.fixangle) {
        Common::MSG_WriteByte(msg, svc_setangle);
        for (int i = 0; i < 3; ++i) Common::MSG_WriteAngle(msg, ent->v.angles[i]);
        ent->v.fixangle = 0;
    }

    int bits = 0;
    if (ent->v.view_ofs[2] != DEFAULT_VIEWHEIGHT) bits |= SU_VIEWHEIGHT;
    if (ent->v.idealpitch) bits |= SU_IDEALPITCH;

    const eval_t* val = VM::GetEdictFieldValue(ent, "items2");
    int items = val ? (static_cast<int>(ent->v.items) | (static_cast<int>(val->_float) << 23))
                    : (static_cast<int>(ent->v.items) | (static_cast<int>(VM::pr_global_struct->serverflags) << 28));

    bits |= SU_ITEMS;
    if (static_cast<int>(ent->v.flags) & FL_ONGROUND) bits |= SU_ONGROUND;
    if (ent->v.waterlevel >= 2) bits |= SU_INWATER;

    for (int i = 0; i < 3; ++i) {
        if (ent->v.punchangle[i]) bits |= (SU_PUNCH1 << i);
        if (ent->v.velocity[i]) bits |= (SU_VELOCITY1 << i);
    }

    if (ent->v.weaponframe) bits |= SU_WEAPONFRAME;
    if (ent->v.armorvalue) bits |= SU_ARMOR;
    bits |= SU_WEAPON;

    Common::MSG_WriteByte(msg, svc_clientdata);
    Common::MSG_WriteShort(msg, bits);

    if (bits & SU_VIEWHEIGHT) Common::MSG_WriteChar(msg, static_cast<int>(ent->v.view_ofs[2]));
    if (bits & SU_IDEALPITCH) Common::MSG_WriteChar(msg, static_cast<int>(ent->v.idealpitch));

    for (int i = 0; i < 3; ++i) {
        if (bits & (SU_PUNCH1 << i)) Common::MSG_WriteChar(msg, static_cast<int>(ent->v.punchangle[i]));
        if (bits & (SU_VELOCITY1 << i)) Common::MSG_WriteChar(msg, static_cast<int>(ent->v.velocity[i] / 16.0f));
    }

    Common::MSG_WriteLong(msg, items);

    if (bits & SU_WEAPONFRAME) Common::MSG_WriteByte(msg, static_cast<int>(ent->v.weaponframe));
    if (bits & SU_ARMOR) Common::MSG_WriteByte(msg, static_cast<int>(ent->v.armorvalue));
    if (bits & SU_WEAPON) Common::MSG_WriteByte(msg, SV_ModelIndex(VM::PR_GetString(ent->v.weaponmodel)));

    Common::MSG_WriteShort(msg, static_cast<int>(ent->v.health));
    Common::MSG_WriteByte(msg, static_cast<int>(ent->v.currentammo));
    Common::MSG_WriteByte(msg, static_cast<int>(ent->v.ammo_shells));
    Common::MSG_WriteByte(msg, static_cast<int>(ent->v.ammo_nails));
    Common::MSG_WriteByte(msg, static_cast<int>(ent->v.ammo_rockets));
    Common::MSG_WriteByte(msg, static_cast<int>(ent->v.ammo_cells));

    if (Common::standard_quake) {
        Common::MSG_WriteByte(msg, static_cast<int>(ent->v.weapon));
    } else {
        for (int i = 0; i < 32; ++i) {
            if (static_cast<int>(ent->v.weapon) & (1 << i)) {
                Common::MSG_WriteByte(msg, i);
                break;
            }
        }
    }
}

qboolean SV_SendClientDatagram(client_t* client)
{
    std::array<byte, MAX_DATAGRAM> buf { };
    sizebuf_t msg { };

    msg.data = buf.data();
    msg.maxsize = static_cast<int>(buf.size());
    msg.cursize = 0;

    Common::MSG_WriteByte(&msg, svc_time);
    Common::MSG_WriteFloat(&msg, static_cast<float>(sv.time));

    SV_WriteClientdataToMessage(client->edict, &msg);
    SV_WriteEntitiesToClient(client->edict, &msg);

    if (msg.cursize + sv.datagram.cursize < msg.maxsize) {
        Common::SZ_Write(&msg, sv.datagram.data, sv.datagram.cursize);
    }

    if (Net::NET_SendUnreliableMessage(client->netconnection, &msg) == -1) {
        SV_DropClient(true);
        return false;
    }

    return true;
}

void SV_UpdateToReliableMessages()
{
    auto clients = svs.GetClients();
    for (size_t i = 0; i < clients.size(); ++i) {
        Host::host_client = &clients[i];
        if (Host::host_client->old_frags != static_cast<int>(Host::host_client->edict->v.frags)) {
            for (auto& client : clients) {
                if (!client.active) continue;
                Common::MSG_WriteByte(&client.message, svc_updatefrags);
                Common::MSG_WriteByte(&client.message, static_cast<int>(i));
                Common::MSG_WriteShort(&client.message, static_cast<int>(Host::host_client->edict->v.frags));
            }
            Host::host_client->old_frags = static_cast<int>(Host::host_client->edict->v.frags);
        }
    }

    for (auto& client : clients) {
        if (!client.active) continue;
        Common::SZ_Write(&client.message, sv.reliable_datagram.data, sv.reliable_datagram.cursize);
    }

    Common::SZ_Clear(&sv.reliable_datagram);
}

void SV_SendNop(client_t* client)
{
    std::array<byte, 4> buf { };
    sizebuf_t msg { };

    msg.data = buf.data();
    msg.maxsize = static_cast<int>(buf.size());
    msg.cursize = 0;

    Common::MSG_WriteChar(&msg, svc_nop);

    if (Net::NET_SendUnreliableMessage(client->netconnection, &msg) == -1) {
        SV_DropClient(true);
    }

    client->last_message = Host::realtime;
}

void SV_SendClientMessages()
{
    SV_UpdateToReliableMessages();

    auto clients = svs.GetClients();
    for (auto& client : clients) {
        Host::host_client = &client;
        if (!Host::host_client->active) continue;

        if (Host::host_client->spawned) {
            if (!SV_SendClientDatagram(Host::host_client)) continue;
        } else {
            if (!Host::host_client->sendsignon) {
                if (Host::realtime - Host::host_client->last_message > 5.0) SV_SendNop(Host::host_client);
                continue;
            }
        }

        if (Host::host_client->message.overflowed) {
            SV_DropClient(true);
            Host::host_client->message.overflowed = false;
            continue;
        }

        if (Host::host_client->message.cursize || Host::host_client->dropasap) {
            if (!Net::NET_CanSendMessage(Host::host_client->netconnection)) continue;

            if (Host::host_client->dropasap) {
                SV_DropClient(false);
            } else {
                if (Net::NET_SendMessage(Host::host_client->netconnection, &Host::host_client->message) == -1) {
                    SV_DropClient(true);
                }

                Common::SZ_Clear(&Host::host_client->message);
                Host::host_client->last_message = Host::realtime;
                Host::host_client->sendsignon = false;
            }
        }
    }

    SV_CleanupEnts();
}

constexpr int MAX_FORWARD = 6;

void SV_SetIdealPitch()
{
    if (!(static_cast<int>(sv_player->v.flags) & FL_ONGROUND)) return;

    const float angleval = static_cast<float>(sv_player->v.angles[YAW] * M_PI * 2.0 / 360.0);
    const float sinval = sinf(angleval);
    const float cosval = cosf(angleval);

    std::array<float, MAX_FORWARD> z { };

    int i = 0;
    for (; i < MAX_FORWARD; ++i) {
        Vector3 top(sv_player->v.origin.x + cosval * (i + 3) * 12.0f, sv_player->v.origin.y + sinval * (i + 3) * 12.0f,
            sv_player->v.origin.z + sv_player->v.view_ofs.z);

        Vector3 bottom = top;
        bottom.z -= 160.0f;

        const trace_t tr = SV_Move(top, Math::vec3_origin, Math::vec3_origin, bottom, MoveMode::NoMonsters, sv_player);
        if (tr.allsolid || tr.fraction == 1.0f) return;

        z[static_cast<size_t>(i)] = top.z + tr.fraction * (bottom.z - top.z);
    }

    int dir = 0, steps = 0;
    for (int j = 1; j < i; ++j) {
        const int step = static_cast<int>(z[static_cast<size_t>(j)] - z[static_cast<size_t>(j - 1)]);
        if (step > -ON_EPSILON && step < ON_EPSILON) continue;
        if (dir && (step - dir > ON_EPSILON || step - dir < -ON_EPSILON)) return;

        steps++;
        dir = step;
    }

    if (!dir) {
        sv_player->v.idealpitch = 0.0f;
        return;
    }

    if (steps < 2) return;

    sv_player->v.idealpitch = -dir * sv_idealpitchscale.value;
}

static void SV_UserFriction()
{
    const float speed = sqrtf(velocity[0] * velocity[0] + velocity[1] * velocity[1]);
    if (!speed) return;

    Vector3 start(origin[0] + velocity[0] / speed * 16.0f, origin[1] + velocity[1] / speed * 16.0f,
        origin[2] + sv_player->v.mins.z);
    Vector3 stop = start;
    stop.z -= 34.0f;

    const trace_t trace = SV_Move(start, Math::vec3_origin, Math::vec3_origin, stop, MoveMode::NoMonsters, sv_player);
    const float friction = (trace.fraction == 1.0f) ? (sv_friction.value * sv_edgefriction.value) : sv_friction.value;

    const float control = (speed < sv_stopspeed.value) ? sv_stopspeed.value : speed;
    float newspeed = static_cast<float>(speed - Host::host_frametime * control * friction);
    if (newspeed < 0.0f) newspeed = 0.0f;

    newspeed /= speed;
    velocity[0] *= newspeed;
    velocity[1] *= newspeed;
    velocity[2] *= newspeed;
}

static void SV_Accelerate()
{
    const float currentspeed = DotProduct(velocity, wishdir);
    const float addspeed = wishspeed - currentspeed;
    if (addspeed <= 0.0f) return;

    float accelspeed = static_cast<float>(sv_accelerate.value * Host::host_frametime * wishspeed);
    if (accelspeed > addspeed) accelspeed = addspeed;

    for (int i = 0; i < 3; ++i) velocity[i] += accelspeed * wishdir[i];
}

static void SV_AirAccelerate(Vector3 wishveloc)
{
    float wishspd = wishveloc.normalize();
    if (wishspd > 30.0f) wishspd = 30.0f;

    const float currentspeed = DotProduct(velocity, wishveloc);
    const float addspeed = wishspd - currentspeed;
    if (addspeed <= 0.0f) return;

    float accelspeed = static_cast<float>(sv_accelerate.value * wishspeed * Host::host_frametime);
    if (accelspeed > addspeed) accelspeed = addspeed;

    for (int i = 0; i < 3; ++i) velocity[i] += accelspeed * wishveloc[i];
}

static void DropPunchAngle()
{
    float len = sv_player->v.punchangle.normalize();
    len -= static_cast<float>(10.0 * Host::host_frametime);
    if (len < 0.0f) len = 0.0f;
    sv_player->v.punchangle *= len;
}

static void SV_WaterMove()
{
    Vector3 forward { }, right { }, up { };
    Math::AngleVectors(sv_player->v.v_angle, forward, right, up);

    Vector3 wishvel = forward * cmd.forwardmove + right * cmd.sidemove;

    if (!cmd.forwardmove && !cmd.sidemove && !cmd.upmove)
        wishvel.z -= 60.0f;
    else
        wishvel.z += cmd.upmove;

    float w_speed = wishvel.length();
    if (w_speed > sv_maxspeed.value) {
        wishvel *= sv_maxspeed.value / w_speed;
        w_speed = sv_maxspeed.value;
    }

    w_speed *= 0.7f;

    const float speed = Math::Length(velocity);
    float newspeed = 0.0f;
    if (speed) {
        newspeed = speed - static_cast<float>(Host::host_frametime * speed * sv_friction.value);
        if (newspeed < 0.0f) newspeed = 0.0f;
        Math::VectorScale(velocity, newspeed / speed, velocity);
    }

    if (!w_speed) return;

    const float addspeed = w_speed - newspeed;
    if (addspeed <= 0.0f) return;

    wishvel.normalize();
    float accelspeed = static_cast<float>(sv_accelerate.value * w_speed * Host::host_frametime);
    if (accelspeed > addspeed) accelspeed = addspeed;

    for (int i = 0; i < 3; ++i) velocity[i] += accelspeed * wishvel[i];
}

static void SV_WaterJump()
{
    if (sv.time > sv_player->v.teleport_time || !sv_player->v.waterlevel) {
        sv_player->v.flags = static_cast<float>(static_cast<int>(sv_player->v.flags) & ~FL_WATERJUMP);
        sv_player->v.teleport_time = 0.0;
    }

    sv_player->v.velocity.x = sv_player->v.movedir.x;
    sv_player->v.velocity.y = sv_player->v.movedir.y;
}

static void SV_AirMove()
{
    Vector3 forward { }, right { }, up { };
    Math::AngleVectors(sv_player->v.angles, forward, right, up);

    float fmove = cmd.forwardmove;
    float smove = cmd.sidemove;

    if (sv.time < sv_player->v.teleport_time && fmove < 0.0f) fmove = 0.0f;

    Vector3 wishvel = forward * fmove + right * smove;
    if (static_cast<int>(sv_player->v.movetype) != MOVETYPE_WALK)
        wishvel.z = cmd.upmove;
    else
        wishvel.z = 0.0f;

    wishdir = wishvel;
    wishspeed = wishdir.normalize();
    if (wishspeed > sv_maxspeed.value) {
        wishvel *= sv_maxspeed.value / wishspeed;
        wishspeed = sv_maxspeed.value;
    }

    if (sv_player->v.movetype == MOVETYPE_NOCLIP) {
        VectorCopy(wishvel, velocity);
    } else if (onground) {
        SV_UserFriction();
        SV_Accelerate();
    } else {
        SV_AirAccelerate(wishvel);
    }
}

void SV_ClientThink()
{
    if (sv_player->v.movetype == MOVETYPE_NONE) return;

    onground = static_cast<int>(sv_player->v.flags) & FL_ONGROUND;
    origin = sv_player->v.origin;
    velocity = sv_player->v.velocity;

    DropPunchAngle();

    if (sv_player->v.health <= 0.0f) return;

    cmd = Host::host_client->cmd;
    angles = sv_player->v.angles;

    const Vector3 v_angle = sv_player->v.v_angle + sv_player->v.punchangle;
    angles[ROLL] = View::V_CalcRoll(sv_player->v.angles, sv_player->v.velocity) * 4.0f;
    if (!sv_player->v.fixangle) {
        angles[PITCH] = -v_angle[PITCH] / 3.0f;
        angles[YAW] = v_angle[YAW];
    }

    if (static_cast<int>(sv_player->v.flags) & FL_WATERJUMP) {
        SV_WaterJump();
        return;
    }

    if ((sv_player->v.waterlevel >= 2) && (sv_player->v.movetype != MOVETYPE_NOCLIP)) {
        SV_WaterMove();
        return;
    }

    SV_AirMove();
}

static void SV_ReadClientMove(usercmd_t* move)
{
    Host::host_client->ping_times[static_cast<size_t>(Host::host_client->num_pings % NUM_PING_TIMES)]
        = static_cast<float>(sv.time) - Common::MSG_ReadFloat();
    Host::host_client->num_pings++;

    Vector3 angle { };
    angle.x = Common::MSG_ReadAngle();
    angle.y = Common::MSG_ReadAngle();
    angle.z = Common::MSG_ReadAngle();
    Host::host_client->edict->v.v_angle = angle;

    move->forwardmove = static_cast<float>(Common::MSG_ReadShort());
    move->sidemove = static_cast<float>(Common::MSG_ReadShort());
    move->upmove = static_cast<float>(Common::MSG_ReadShort());

    const int bits = Common::MSG_ReadByte();
    Host::host_client->edict->v.button0 = static_cast<float>(bits & 1);
    Host::host_client->edict->v.button2 = static_cast<float>((bits & 2) >> 1);

    const int impulse = Common::MSG_ReadByte();
    if (impulse) Host::host_client->edict->v.impulse = static_cast<float>(impulse);
}

static qboolean SV_ReadClientMessage()
{
    static constexpr std::array<std::string_view, 19> allowed_commands
        = { "status", "god", "notarget", "fly", "name", "noclip", "say", "say_team", "tell", "color", "kill", "pause",
              "spawn", "begin", "prespawn", "kick", "ping", "give", "ban" };

    int ret = 0;
    do {
    nextmsg:
        ret = Net::NET_GetMessage(Host::host_client->netconnection);
        if (ret == -1) {
            Common::Sys_Printf("SV_ReadClientMessage: NET_GetMessage failed\n");
            return false;
        }

        if (!ret) return true;

        Common::MSG_BeginReading();

        while (true) {
            if (!Host::host_client->active) return false;

            if (Common::msg_badread) {
                Common::Sys_Printf("SV_ReadClientMessage: badread\n");
                return false;
            }

            const int msg_cmd = Common::MSG_ReadChar();

            switch (msg_cmd) {
            case -1:
                goto nextmsg;

            default:
                Common::Sys_Printf("SV_ReadClientMessage: unknown command char\n");
                return false;

            case clc_nop:
                break;

            case clc_stringcmd: {
                const char* s = Common::MSG_ReadString();
                ret = Host::host_client->privileged ? 2 : 0;

                const std::string_view cmd_sv(s);
                for (const auto& allowed : allowed_commands) {
                    if (cmd_sv.length() >= allowed.length()
                        && Common::Q_strncasecmp(s, allowed.data(), static_cast<int>(allowed.length())) == 0) {
                        ret = 1;
                        break;
                    }
                }

                if (ret == 2) {
                    Cmd::BufferInsertText(s);
                } else if (ret == 1) {
                    Cmd::ExecuteString(s, Cmd::Source::Client);
                } else {
                    Console::Con_DPrintf("%s tried to %s\n", Host::host_client->name.data(), s);
                }
                break;
            }

            case clc_disconnect:
                return false;

            case clc_move:
                SV_ReadClientMove(&Host::host_client->cmd);
                break;
            }
        }
    } while (ret == 1);

    return true;
}

void SV_RunClients()
{
    auto clients = svs.GetClients();
    for (auto& client : clients) {
        Host::host_client = &client;
        if (!Host::host_client->active) continue;

        sv_player = Host::host_client->edict;

        if (!SV_ReadClientMessage()) {
            SV_DropClient(false);
            continue;
        }

        if (!Host::host_client->spawned) {
            Host::host_client->cmd = usercmd_t { };
            continue;
        }

        if (!sv.paused && (svs.maxclients > 1 || Keys::key_dest == Keys::key_game)) {
            SV_ClientThink();
        }
    }
}

void SV_ClientPrintf(const char* fmt, ...)
{
    va_list argptr;
    char string[1024];

    va_start(argptr, fmt);
    vsprintf_s(string, sizeof(string), fmt, argptr);
    va_end(argptr);

    Common::MSG_WriteByte(&Host::host_client->message, svc_print);
    Common::MSG_WriteString(&Host::host_client->message, string);
}

void SV_BroadcastPrintf(const char* fmt, ...)
{
    va_list argptr;
    char string[1024];

    va_start(argptr, fmt);
    vsprintf_s(string, sizeof(string), fmt, argptr);
    va_end(argptr);

    for (int i = 0; i < svs.maxclients; i++) {
        if (svs.clients[i].active && svs.clients[i].spawned) {
            Common::MSG_WriteByte(&svs.clients[i].message, svc_print);
            Common::MSG_WriteString(&svs.clients[i].message, string);
        }
    }
}

void SV_DropClient(bool crash)
{
    if (!crash) {
        if (Net::NET_CanSendMessage(Host::host_client->netconnection)) {
            Common::MSG_WriteByte(&Host::host_client->message, svc_disconnect);
            Net::NET_SendMessage(Host::host_client->netconnection, &Host::host_client->message);
        }

        if (Host::host_client->edict && Host::host_client->spawned) {
            int saveSelf = VM::pr_global_struct->self;
            VM::pr_global_struct->self = static_cast<int>(EDICT_TO_PROG(Host::host_client->edict));
            VM::PR_ExecuteProgram(VM::pr_global_struct->ClientDisconnect);
            VM::pr_global_struct->self = saveSelf;
        }

        Common::Sys_Printf("Client %s removed\n", Host::host_client->name.data());
    }

    Net::NET_Close(Host::host_client->netconnection);
    Host::host_client->netconnection = nullptr;

    Host::host_client->active = false;
    Host::host_client->name[0] = 0;
    Host::host_client->old_frags = -999999;
    Net::net_activeconnections--;

    for (int i = 0; i < svs.maxclients; i++) {
        client_t* client = &svs.clients[i];
        if (!client->active) {
            continue;
        }

        Common::MSG_WriteByte(&client->message, svc_updatename);
        Common::MSG_WriteByte(&client->message, static_cast<int>(Host::host_client - svs.clients));
        Common::MSG_WriteString(&client->message, "");
        Common::MSG_WriteByte(&client->message, svc_updatefrags);
        Common::MSG_WriteByte(&client->message, static_cast<int>(Host::host_client - svs.clients));
        Common::MSG_WriteShort(&client->message, 0);
        Common::MSG_WriteByte(&client->message, svc_updatecolors);
        Common::MSG_WriteByte(&client->message, static_cast<int>(Host::host_client - svs.clients));
        Common::MSG_WriteByte(&client->message, 0);
    }

    Host::host_client->netconnection = nullptr;
}

} // namespace Server
