// server.cpp -- server subsystem (merged from sv_main.cpp, sv_phys.cpp, sv_move.cpp, sv_user.cpp)

#include "quakedef.hpp"

//============================================================================
// Global variable definitions
//============================================================================

// from sv_main.cpp
server_t sv;
server_static_t svs;
char localmodels[MAX_MODELS][5];
int fatbytes;
byte fatpvs[MAX_MAP_LEAFS / 8];

// from sv_phys.cpp
cvar_t sv_friction = { "sv_friction", "4", false, true };
cvar_t sv_stopspeed = { "sv_stopspeed", "100" };
cvar_t sv_gravity = { "sv_gravity", "800", false, true };
cvar_t sv_maxvelocity = { "sv_maxvelocity", "2000" };
cvar_t sv_nostep = { "sv_nostep", "0" };

// from sv_move.cpp
int c_yes, c_no;

// from sv_user.cpp
edict_t* sv_player;
cvar_t sv_edgefriction = { "edgefriction", "2" };
vec3_t wishdir;
float wishspeed;
float* angles;
float* origin;
float* velocity;
qboolean onground;
usercmd_t cmd;
cvar_t sv_idealpitchscale = { "sv_idealpitchscale", "0.8" };
cvar_t sv_maxspeed = { "sv_maxspeed", "320", false, true };
cvar_t sv_accelerate = { "sv_accelerate", "10" };

//============================================================================
// sv_main.cpp contents
//============================================================================

namespace VM { extern cvar_t sv_aim; }

/*
===============
SV_Init
===============
*/
void SV_Init(void)
{
    int i;

    Cvar_RegisterVariable(&sv_maxvelocity);
    Cvar_RegisterVariable(&sv_gravity);
    Cvar_RegisterVariable(&sv_friction);
    Cvar_RegisterVariable(&sv_edgefriction);
    Cvar_RegisterVariable(&sv_stopspeed);
    Cvar_RegisterVariable(&sv_maxspeed);
    Cvar_RegisterVariable(&sv_accelerate);
    Cvar_RegisterVariable(&sv_idealpitchscale);
    Cvar_RegisterVariable(&sv_aim);
    Cvar_RegisterVariable(&sv_nostep);

    for (i = 0; i < MAX_MODELS; i++) {
        sprintf(localmodels[i], "*%i", i);
    }
}

/*
==================
SV_StartParticle

Make sure the event gets sent to all clients
==================
*/
void SV_StartParticle(vec3_t org, vec3_t dir, int color, int count)
{
    int i, v;

    if (sv.datagram.cursize > MAX_DATAGRAM - 16) {
        return;
    }

    MSG_WriteByte(&sv.datagram, svc_particle);
    MSG_WriteCoord(&sv.datagram, org[0]);
    MSG_WriteCoord(&sv.datagram, org[1]);
    MSG_WriteCoord(&sv.datagram, org[2]);
    for (i = 0; i < 3; i++) {
        v = dir[i] * 16;
        if (v > 127) {
            v = 127;
        } else if (v < -128) {
            v = -128;
        }

        MSG_WriteChar(&sv.datagram, v);
    }
    MSG_WriteByte(&sv.datagram, count);
    MSG_WriteByte(&sv.datagram, color);
}

/*
==================
SV_StartSound

Each entity can have eight independant sound sources, like voice,
weapon, feet, etc.

Channel 0 is an auto-allocate channel, the others override anything
allready running on that entity/channel pair.

An attenuation of 0 will play full volume everywhere in the level.
Larger attenuations will drop off.  (max 4 attenuation)

==================
*/
void SV_StartSound(edict_t* entity,
    int channel,
    char* sample,
    int vol,
    float attenuation)
{
    int sound_num;
    int field_mask;
    int i;
    int ent;

    if (vol < 0 || vol > 255) {
        Sys_Error("SV_StartSound: volume = %i", vol);
    }

    if (attenuation < 0 || attenuation > 4) {
        Sys_Error("SV_StartSound: attenuation = %f", attenuation);
    }

    if (channel < 0 || channel > 7) {
        Sys_Error("SV_StartSound: channel = %i", channel);
    }

    if (sv.datagram.cursize > MAX_DATAGRAM - 16) {
        return;
    }

    // find precache number for sound
    for (sound_num = 1; sound_num < MAX_SOUNDS && sv.sound_precache[sound_num];
        sound_num++) {
        if (!strcmp(sample, sv.sound_precache[sound_num])) {
            break;
        }
    }

    if (sound_num == MAX_SOUNDS || !sv.sound_precache[sound_num]) {
        Con_Printf("SV_StartSound: %s not precacheed\n", sample);

        return;
    }

    ent = NUM_FOR_EDICT(entity);

    channel = (ent << 3) | channel;

    field_mask = 0;
    if (vol != DEFAULT_SOUND_PACKET_VOLUME) {
        field_mask |= SND_VOLUME;
    }

    if (attenuation != DEFAULT_SOUND_PACKET_ATTENUATION) {
        field_mask |= SND_ATTENUATION;
    }

    // directed messages go only to the entity the are targeted on
    MSG_WriteByte(&sv.datagram, svc_sound);
    MSG_WriteByte(&sv.datagram, field_mask);
    if (field_mask & SND_VOLUME) {
        MSG_WriteByte(&sv.datagram, vol);
    }

    if (field_mask & SND_ATTENUATION) {
        MSG_WriteByte(&sv.datagram, attenuation * 64);
    }

    MSG_WriteShort(&sv.datagram, channel);
    MSG_WriteByte(&sv.datagram, sound_num);
    for (i = 0; i < 3; i++) {
        MSG_WriteCoord(
            &sv.datagram,
            entity->v.origin[i] + 0.5 * (entity->v.mins[i] + entity->v.maxs[i]));
    }
}

/*
================
SV_SendServerinfo

Sends the first message from the server to a connected client.
This will be sent on the initial connection and upon each server load.
================
*/
void SV_SendServerinfo(client_t* client)
{
    char** s;
    char message[2048];

    MSG_WriteByte(&client->message, svc_print);
    sprintf(message, "%c\nVERSION %4.2f SERVER (%i CRC)", 2, VERSION, pr_crc);
    MSG_WriteString(&client->message, message);

    MSG_WriteByte(&client->message, svc_serverinfo);
    MSG_WriteLong(&client->message, PROTOCOL_VERSION);
    MSG_WriteByte(&client->message, svs.maxclients);

    if (!coop.value && deathmatch.value) {
        MSG_WriteByte(&client->message, GAME_DEATHMATCH);
    } else {
        MSG_WriteByte(&client->message, GAME_COOP);
    }

    sprintf(message, PR_GetString(sv.edicts->v.message));

    MSG_WriteString(&client->message, message);

    for (s = sv.model_precache + 1; *s; s++) {
        MSG_WriteString(&client->message, *s);
    }
    MSG_WriteByte(&client->message, 0);

    for (s = sv.sound_precache + 1; *s; s++) {
        MSG_WriteString(&client->message, *s);
    }
    MSG_WriteByte(&client->message, 0);

    // send music
    MSG_WriteByte(&client->message, svc_cdtrack);
    MSG_WriteByte(&client->message, sv.edicts->v.sounds);
    MSG_WriteByte(&client->message, sv.edicts->v.sounds);

    // set view
    MSG_WriteByte(&client->message, svc_setview);
    MSG_WriteShort(&client->message, NUM_FOR_EDICT(client->edict));

    MSG_WriteByte(&client->message, svc_signonnum);
    MSG_WriteByte(&client->message, 1);

    client->sendsignon = true;
    client->spawned = false; // need prespawn, spawn, etc
}

/*
================
SV_ConnectClient

Initializes a client_t for a new net connection.  This will only be called
once for a player each game, not once for each level change.
================
*/
void SV_ConnectClient(int clientnum)
{
    edict_t* ent;
    client_t* client;
    int edictnum;
    struct qsocket_s* netconnection;
    int i;
    float spawn_parms[NUM_SPAWN_PARMS];

    client = svs.clients + clientnum;

    Con_DPrintf("Client %s connected\n", client->netconnection->address);

    edictnum = clientnum + 1;

    ent = EDICT_NUM(edictnum);

    // set up the client_t
    netconnection = client->netconnection;

    if (sv.loadgame) {
        memcpy(spawn_parms, client->spawn_parms, sizeof(spawn_parms));
    }

    memset(client, 0, sizeof(*client));
    client->netconnection = netconnection;

    strcpy(client->name, "unconnected");
    client->active = true;
    client->spawned = false;
    client->edict = ent;
    client->message.data = client->msgbuf;
    client->message.maxsize = sizeof(client->msgbuf);
    client->message.allowoverflow = true; // we can catch it

    client->privileged = false;

    if (sv.loadgame) {
        memcpy(client->spawn_parms, spawn_parms, sizeof(spawn_parms));
    } else {
        // call the progs to get default spawn parms for the new client
        PR_ExecuteProgram(pr_global_struct->SetNewParms);
        for (i = 0; i < NUM_SPAWN_PARMS; i++) {
            client->spawn_parms[i] = (&pr_global_struct->parm1)[i];
        }
    }

    SV_SendServerinfo(client);
}

/*
===================
SV_CheckForNewClients

===================
*/
void SV_CheckForNewClients(void)
{
    struct qsocket_s* ret;
    int i;

    //
    // check for new connections
    //
    while (1) {
        ret = NET_CheckNewConnections();
        if (!ret) {
            break;
        }

        //
        // init a new client structure
        //
        for (i = 0; i < svs.maxclients; i++) {
            if (!svs.clients[i].active) {
                break;
            }
        }
        if (i == svs.maxclients) {
            Sys_Error("Host_CheckForNewClients: no free clients");
        }

        svs.clients[i].netconnection = ret;
        SV_ConnectClient(i);

        net_activeconnections++;
    }
}

//=============================================================================
// PVS
//=============================================================================

void SV_AddToFatPVS(vec3_t org, mnode_t* node)
{
    int i;
    byte* pvs;
    mplane_t* plane;
    float d;

    while (1) {
        // if this is a leaf, accumulate the pvs bits
        if (node->contents < 0) {
            if (node->contents != CONTENTS_SOLID) {
                pvs = Mod_LeafPVS((mleaf_t*)node, sv.worldmodel);
                for (i = 0; i < fatbytes; i++) {
                    fatpvs[i] |= pvs[i];
                }
            }

            return;
        }

        plane = node->plane;
        d = DotProduct(org, plane->normal) - plane->dist;
        if (d > 8) {
            node = node->children[0];
        } else if (d < -8) {
            node = node->children[1];
        } else { // go down both
            SV_AddToFatPVS(org, node->children[0]);
            node = node->children[1];
        }
    }
}

/*
=============
SV_FatPVS

Calculates a PVS that is the inclusive or of all leafs within 8 pixels of the
given point.
=============
*/
byte* SV_FatPVS(vec3_t org)
{
    fatbytes = (sv.worldmodel->numleafs + 31) >> 3;
    Q_memset(fatpvs, 0, fatbytes);
    SV_AddToFatPVS(org, sv.worldmodel->nodes);

    return fatpvs;
}

//=============================================================================

/*
=============
SV_WriteEntitiesToClient

=============
*/
void SV_WriteEntitiesToClient(edict_t* clent, sizebuf_t* msg)
{
    int e, i;
    int bits;
    byte* pvs;
    vec3_t org;
    float miss;
    edict_t* ent;

    // find the client's PVS
    VectorAdd(clent->v.origin, clent->v.view_ofs, org);
    pvs = SV_FatPVS(org);

    // send over all entities (excpet the client) that touch the pvs
    ent = NEXT_EDICT(sv.edicts);
    for (e = 1; e < sv.num_edicts; e++, ent = NEXT_EDICT(ent)) {

        // ignore if not touching a PV leaf
        if (ent != clent) // clent is ALLWAYS sent
        {
            // ignore ents without visible models
            if (!ent->v.modelindex || !*PR_GetString(ent->v.model)) {
                continue;
            }

            for (i = 0; i < ent->num_leafs; i++) {
                if (pvs[ent->leafnums[i] >> 3] & (1 << (ent->leafnums[i] & 7))) {
                    break;
                }
            }

            if (i == ent->num_leafs) {
                continue; // not visible
            }
        }

        if (msg->maxsize - msg->cursize < 16) {
            Con_Printf("packet overflow\n");

            return;
        }

        // send an update
        bits = 0;

        for (i = 0; i < 3; i++) {
            miss = ent->v.origin[i] - ent->baseline.origin[i];
            if (miss < -0.1 || miss > 0.1) {
                bits |= U_ORIGIN1 << i;
            }
        }

        if (ent->v.angles[0] != ent->baseline.angles[0]) {
            bits |= U_ANGLE1;
        }

        if (ent->v.angles[1] != ent->baseline.angles[1]) {
            bits |= U_ANGLE2;
        }

        if (ent->v.angles[2] != ent->baseline.angles[2]) {
            bits |= U_ANGLE3;
        }

        if (ent->v.movetype == MOVETYPE_STEP) {
            bits |= U_NOLERP; // don't mess up the step animation
        }

        if (ent->baseline.colormap != ent->v.colormap) {
            bits |= U_COLORMAP;
        }

        if (ent->baseline.skin != ent->v.skin) {
            bits |= U_SKIN;
        }

        if (ent->baseline.frame != ent->v.frame) {
            bits |= U_FRAME;
        }

        if (ent->baseline.effects != ent->v.effects) {
            bits |= U_EFFECTS;
        }

        if (ent->baseline.modelindex != ent->v.modelindex) {
            bits |= U_MODEL;
        }

        if (e >= 256) {
            bits |= U_LONGENTITY;
        }

        if (bits >= 256) {
            bits |= U_MOREBITS;
        }

        //
        // write the message
        //
        MSG_WriteByte(msg, bits | U_SIGNAL);

        if (bits & U_MOREBITS) {
            MSG_WriteByte(msg, bits >> 8);
        }

        if (bits & U_LONGENTITY) {
            MSG_WriteShort(msg, e);
        } else {
            MSG_WriteByte(msg, e);
        }

        if (bits & U_MODEL) {
            MSG_WriteByte(msg, ent->v.modelindex);
        }

        if (bits & U_FRAME) {
            MSG_WriteByte(msg, ent->v.frame);
        }

        if (bits & U_COLORMAP) {
            MSG_WriteByte(msg, ent->v.colormap);
        }

        if (bits & U_SKIN) {
            MSG_WriteByte(msg, ent->v.skin);
        }

        if (bits & U_EFFECTS) {
            MSG_WriteByte(msg, ent->v.effects);
        }

        if (bits & U_ORIGIN1) {
            MSG_WriteCoord(msg, ent->v.origin[0]);
        }

        if (bits & U_ANGLE1) {
            MSG_WriteAngle(msg, ent->v.angles[0]);
        }

        if (bits & U_ORIGIN2) {
            MSG_WriteCoord(msg, ent->v.origin[1]);
        }

        if (bits & U_ANGLE2) {
            MSG_WriteAngle(msg, ent->v.angles[1]);
        }

        if (bits & U_ORIGIN3) {
            MSG_WriteCoord(msg, ent->v.origin[2]);
        }

        if (bits & U_ANGLE3) {
            MSG_WriteAngle(msg, ent->v.angles[2]);
        }
    }
}

/*
=============
SV_CleanupEnts

=============
*/
void SV_CleanupEnts(void)
{
    int e;
    edict_t* ent;

    ent = NEXT_EDICT(sv.edicts);
    for (e = 1; e < sv.num_edicts; e++, ent = NEXT_EDICT(ent)) {
        ent->v.effects = (int)ent->v.effects & ~EF_MUZZLEFLASH;
    }
}

/*
==================
SV_WriteClientdataToMessage

==================
*/
void SV_WriteClientdataToMessage(edict_t* ent, sizebuf_t* msg)
{
    int bits;
    int i;
    edict_t* other;
    int items;
    eval_t* val;

    //
    // send a damage message
    //
    if (ent->v.dmg_take || ent->v.dmg_save) {
        other = PROG_TO_EDICT(ent->v.dmg_inflictor);
        MSG_WriteByte(msg, svc_damage);
        MSG_WriteByte(msg, ent->v.dmg_save);
        MSG_WriteByte(msg, ent->v.dmg_take);
        for (i = 0; i < 3; i++) {
            MSG_WriteCoord(msg, other->v.origin[i] + 0.5 * (other->v.mins[i] + other->v.maxs[i]));
        }

        ent->v.dmg_take = 0;
        ent->v.dmg_save = 0;
    }

    //
    // send the current viewpos offset from the view entity
    //
    SV_SetIdealPitch(); // how much to look up / down ideally

    // a fixangle might get lost in a dropped packet.  Oh well.
    if (ent->v.fixangle) {
        MSG_WriteByte(msg, svc_setangle);
        for (i = 0; i < 3; i++) {
            MSG_WriteAngle(msg, ent->v.angles[i]);
        }
        ent->v.fixangle = 0;
    }

    bits = 0;

    if (ent->v.view_ofs[2] != DEFAULT_VIEWHEIGHT) {
        bits |= SU_VIEWHEIGHT;
    }

    if (ent->v.idealpitch) {
        bits |= SU_IDEALPITCH;
    }

// stuff the sigil bits into the high bits of items for sbar, or else
// mix in items2
    val = GetEdictFieldValue(ent, "items2");

    if (val) {
        items = (int)ent->v.items | ((int)val->_float << 23);
    } else {
        items = (int)ent->v.items | ((int)pr_global_struct->serverflags << 28);
    }


    bits |= SU_ITEMS;

    if ((int)ent->v.flags & FL_ONGROUND) {
        bits |= SU_ONGROUND;
    }

    if (ent->v.waterlevel >= 2) {
        bits |= SU_INWATER;
    }

    for (i = 0; i < 3; i++) {
        if (ent->v.punchangle[i]) {
            bits |= (SU_PUNCH1 << i);
        }

        if (ent->v.velocity[i]) {
            bits |= (SU_VELOCITY1 << i);
        }
    }

    if (ent->v.weaponframe) {
        bits |= SU_WEAPONFRAME;
    }

    if (ent->v.armorvalue) {
        bits |= SU_ARMOR;
    }

    //	if (ent->v.weapon)
    bits |= SU_WEAPON;

    // send the data

    MSG_WriteByte(msg, svc_clientdata);
    MSG_WriteShort(msg, bits);

    if (bits & SU_VIEWHEIGHT) {
        MSG_WriteChar(msg, ent->v.view_ofs[2]);
    }

    if (bits & SU_IDEALPITCH) {
        MSG_WriteChar(msg, ent->v.idealpitch);
    }

    for (i = 0; i < 3; i++) {
        if (bits & (SU_PUNCH1 << i)) {
            MSG_WriteChar(msg, ent->v.punchangle[i]);
        }

        if (bits & (SU_VELOCITY1 << i)) {
            MSG_WriteChar(msg, ent->v.velocity[i] / 16);
        }
    }

    // [always sent]	if (bits & SU_ITEMS)
    MSG_WriteLong(msg, items);

    if (bits & SU_WEAPONFRAME) {
        MSG_WriteByte(msg, ent->v.weaponframe);
    }

    if (bits & SU_ARMOR) {
        MSG_WriteByte(msg, ent->v.armorvalue);
    }

    if (bits & SU_WEAPON) {
        MSG_WriteByte(msg, SV_ModelIndex(PR_GetString(ent->v.weaponmodel)));
    }

    MSG_WriteShort(msg, ent->v.health);
    MSG_WriteByte(msg, ent->v.currentammo);
    MSG_WriteByte(msg, ent->v.ammo_shells);
    MSG_WriteByte(msg, ent->v.ammo_nails);
    MSG_WriteByte(msg, ent->v.ammo_rockets);
    MSG_WriteByte(msg, ent->v.ammo_cells);

    if (standard_quake) {
        MSG_WriteByte(msg, ent->v.weapon);
    } else {
        for (i = 0; i < 32; i++) {
            if (((int)ent->v.weapon) & (1 << i)) {
                MSG_WriteByte(msg, i);
                break;
            }
        }
    }
}

/*
=======================
SV_SendClientDatagram
=======================
*/
qboolean SV_SendClientDatagram(client_t* client)
{
    byte buf[MAX_DATAGRAM];
    sizebuf_t msg;

    msg.data = buf;
    msg.maxsize = sizeof(buf);
    msg.cursize = 0;

    MSG_WriteByte(&msg, svc_time);
    MSG_WriteFloat(&msg, sv.time);

    // add the client specific data to the datagram
    SV_WriteClientdataToMessage(client->edict, &msg);

    SV_WriteEntitiesToClient(client->edict, &msg);

    // copy the server datagram if there is space
    if (msg.cursize + sv.datagram.cursize < msg.maxsize) {
        SZ_Write(&msg, sv.datagram.data, sv.datagram.cursize);
    }

    // send the datagram
    if (NET_SendUnreliableMessage(client->netconnection, &msg) == -1) {
        SV_DropClient(true); // if the message couldn't send, kick off

        return false;
    }

    return true;
}

/*
=======================
SV_UpdateToReliableMessages
=======================
*/
void SV_UpdateToReliableMessages(void)
{
    int i, j;
    client_t* client;

    // check for changes to be sent over the reliable streams
    for (i = 0, host_client = svs.clients; i < svs.maxclients;
        i++, host_client++) {
        if (host_client->old_frags != host_client->edict->v.frags) {
            for (j = 0, client = svs.clients; j < svs.maxclients; j++, client++) {
                if (!client->active) {
                    continue;
                }

                MSG_WriteByte(&client->message, svc_updatefrags);
                MSG_WriteByte(&client->message, i);
                MSG_WriteShort(&client->message, host_client->edict->v.frags);
            }

            host_client->old_frags = host_client->edict->v.frags;
        }
    }

    for (j = 0, client = svs.clients; j < svs.maxclients; j++, client++) {
        if (!client->active) {
            continue;
        }

        SZ_Write(&client->message, sv.reliable_datagram.data,
            sv.reliable_datagram.cursize);
    }

    SZ_Clear(&sv.reliable_datagram);
}

/*
=======================
SV_SendNop

Send a nop message without trashing or sending the accumulated client
message buffer
=======================
*/
void SV_SendNop(client_t* client)
{
    sizebuf_t msg;
    byte buf[4];

    msg.data = buf;
    msg.maxsize = sizeof(buf);
    msg.cursize = 0;

    MSG_WriteChar(&msg, svc_nop);

    if (NET_SendUnreliableMessage(client->netconnection, &msg) == -1) {
        SV_DropClient(true); // if the message couldn't send, kick off
    }

    client->last_message = realtime;
}

/*
=======================
SV_SendClientMessages
=======================
*/
void SV_SendClientMessages(void)
{
    int i;

    // update frags, names, etc
    SV_UpdateToReliableMessages();

    // build individual updates
    for (i = 0, host_client = svs.clients; i < svs.maxclients;
        i++, host_client++) {
        if (!host_client->active) {
            continue;
        }

        if (host_client->spawned) {
            if (!SV_SendClientDatagram(host_client)) {
                continue;
            }
        } else {
            // the player isn't totally in the game yet
            // send small keepalive messages if too much time has passed
            // send a full message when the next signon stage has been requested
            // some other message data (name changes, etc) may accumulate
            // between signon stages
            if (!host_client->sendsignon) {
                if (realtime - host_client->last_message > 5) {
                    SV_SendNop(host_client);
                }

                continue; // don't send out non-signon messages
            }
        }

        // check for an overflowed message.  Should only happen
        // on a very fucked up connection that backs up a lot, then
        // changes level
        if (host_client->message.overflowed) {
            SV_DropClient(true);
            host_client->message.overflowed = false;
            continue;
        }

        if (host_client->message.cursize || host_client->dropasap) {
            if (!NET_CanSendMessage(host_client->netconnection)) {
                //				I_Printf ("can't write\n");
                continue;
            }

            if (host_client->dropasap) {
                SV_DropClient(false); // went to another level
            } else {
                if (NET_SendMessage(host_client->netconnection,
                        &host_client->message)
                    == -1) {
                    SV_DropClient(true); // if the message couldn't send, kick off
                }

                SZ_Clear(&host_client->message);
                host_client->last_message = realtime;
                host_client->sendsignon = false;
            }
        }
    }

    // clear muzzle flashes
    SV_CleanupEnts();
}

//==============================================================================
// SERVER SPAWNING
//==============================================================================

/*
================
SV_ModelIndex

================
*/
int SV_ModelIndex(char* name)
{
    int i;

    if (!name || !name[0]) {
        return 0;
    }

    for (i = 0; i < MAX_MODELS && sv.model_precache[i]; i++) {
        if (!strcmp(sv.model_precache[i], name)) {
            return i;
        }
    }
    if (i == MAX_MODELS || !sv.model_precache[i]) {
        Sys_Error("SV_ModelIndex: model %s not precached", name);
    }

    return i;
}

/*
================
SV_CreateBaseline

================
*/
void SV_CreateBaseline(void)
{
    int i;
    edict_t* svent;
    int entnum;

    for (entnum = 0; entnum < sv.num_edicts; entnum++) {
        // get the current server version
        svent = EDICT_NUM(entnum);
        if (svent->free) {
            continue;
        }

        if (entnum > svs.maxclients && !svent->v.modelindex) {
            continue;
        }

        //
        // create entity baseline
        //
        VectorCopy(svent->v.origin, svent->baseline.origin);
        VectorCopy(svent->v.angles, svent->baseline.angles);
        svent->baseline.frame = svent->v.frame;
        svent->baseline.skin = svent->v.skin;
        if (entnum > 0 && entnum <= svs.maxclients) {
            svent->baseline.colormap = entnum;
            svent->baseline.modelindex = SV_ModelIndex("progs/player.mdl");
        } else {
            svent->baseline.colormap = 0;
            svent->baseline.modelindex = SV_ModelIndex(PR_GetString(svent->v.model));
        }

        //
        // add to the message
        //
        MSG_WriteByte(&sv.signon, svc_spawnbaseline);
        MSG_WriteShort(&sv.signon, entnum);

        MSG_WriteByte(&sv.signon, svent->baseline.modelindex);
        MSG_WriteByte(&sv.signon, svent->baseline.frame);
        MSG_WriteByte(&sv.signon, svent->baseline.colormap);
        MSG_WriteByte(&sv.signon, svent->baseline.skin);
        for (i = 0; i < 3; i++) {
            MSG_WriteCoord(&sv.signon, svent->baseline.origin[i]);
            MSG_WriteAngle(&sv.signon, svent->baseline.angles[i]);
        }
    }
}

/*
================
SV_SendReconnect

Tell all the clients that the server is changing levels
================
*/
void SV_SendReconnect(void)
{
    char data[128];
    sizebuf_t msg;

    msg.data = (byte*)data;
    msg.cursize = 0;
    msg.maxsize = sizeof(data);

    MSG_WriteChar(&msg, svc_stufftext);
    MSG_WriteString(&msg, "reconnect\n");
    NET_SendToAll(&msg, 5);

    if (cls.state != ca_dedicated)
    {
        Cmd_ExecuteString("reconnect\n", src_command);
    }
}

/*
================
SV_SaveSpawnparms

Grabs the current state of each client for saving across the
transition to another level
================
*/
void SV_SaveSpawnparms(void)
{
    int i, j;

    svs.serverflags = pr_global_struct->serverflags;

    for (i = 0, host_client = svs.clients; i < svs.maxclients;
        i++, host_client++) {
        if (!host_client->active) {
            continue;
        }

        // call the progs to get default spawn parms for the new client
        pr_global_struct->self = EDICT_TO_PROG(host_client->edict);
        PR_ExecuteProgram(pr_global_struct->SetChangeParms);
        for (j = 0; j < NUM_SPAWN_PARMS; j++) {
            host_client->spawn_parms[j] = (&pr_global_struct->parm1)[j];
        }
    }
}

/*
================
SV_SpawnServer

This is called at the start of each level
================
*/
extern float scr_centertime_off;

void SV_SpawnServer(char* server)
{
    edict_t* ent;
    int i;

    // let's not have any servers with no name
    if (hostname.string[0] == 0) {
        Cvar_Set("hostname", "UNNAMED");
    }

    scr_centertime_off = 0;

    Con_DPrintf("SpawnServer: %s\n", server);
    svs.changelevel_issued = false; // now safe to issue another

    //
    // tell all connected clients that we are going to a new level
    //
    if (sv.active) {
        SV_SendReconnect();
    }

    //
    // make cvars consistant
    //
    if (coop.value) {
        Cvar_SetValue("deathmatch", 0);
    }

    current_skill = (int)(skill.value + 0.5);
    if (current_skill < 0) {
        current_skill = 0;
    }

    if (current_skill > 3) {
        current_skill = 3;
    }

    Cvar_SetValue("skill", (float)current_skill);

    //
    // set up the new server
    //
    Host_ClearMemory();

    memset(&sv, 0, sizeof(sv));

    strcpy(sv.name, server);

    // load progs to get entity field count
    PR_LoadProgs();

    // allocate server memory
    sv.max_edicts = MAX_EDICTS;

    sv.edicts = (edict_t *) Hunk_Alloc(sv.max_edicts * pr_edict_size, "edicts");

    sv.datagram.maxsize = sizeof(sv.datagram_buf);
    sv.datagram.cursize = 0;
    sv.datagram.data = sv.datagram_buf;

    sv.reliable_datagram.maxsize = sizeof(sv.reliable_datagram_buf);
    sv.reliable_datagram.cursize = 0;
    sv.reliable_datagram.data = sv.reliable_datagram_buf;

    sv.signon.maxsize = sizeof(sv.signon_buf);
    sv.signon.cursize = 0;
    sv.signon.data = sv.signon_buf;

    // leave slots at start for clients only
    sv.num_edicts = svs.maxclients + 1;
    for (i = 0; i < svs.maxclients; i++) {
        ent = EDICT_NUM(i + 1);
        svs.clients[i].edict = ent;
    }

    sv.state = ss_loading;
    sv.paused = false;

    sv.time = 1.0;

    strcpy(sv.name, server);
    sprintf(sv.modelname, "maps/%s.bsp", server);
    sv.worldmodel = Mod_ForName(sv.modelname, false);
    if (!sv.worldmodel) {
        Con_Printf("Couldn't spawn server %s\n", sv.modelname);
        sv.active = false;

        return;
    }

    sv.models[1] = sv.worldmodel;

    //
    // clear world interaction links
    //
    SV_ClearWorld();

    sv.sound_precache[0] = pr_strings;

    sv.model_precache[0] = pr_strings;
    sv.model_precache[1] = sv.modelname;
    for (i = 1; i < sv.worldmodel->numsubmodels; i++) {
        sv.model_precache[1 + i] = localmodels[i];
        sv.models[i + 1] = Mod_ForName(localmodels[i], false);
    }

    //
    // load the rest of the entities
    //
    ent = EDICT_NUM(0);
    memset(&ent->v, 0, progs->entityfields * 4);
    ent->free = false;
    ent->v.model = PR_SetString(sv.worldmodel->name);
    ent->v.modelindex = 1; // world model
    ent->v.solid = SOLID_BSP;
    ent->v.movetype = MOVETYPE_PUSH;

    if (coop.value) {
        pr_global_struct->coop = coop.value;
    } else {
        pr_global_struct->deathmatch = deathmatch.value;
    }

    pr_global_struct->mapname = PR_SetString(sv.name);

    // serverflags are for cross level information (sigils)
    pr_global_struct->serverflags = svs.serverflags;

    ED_LoadFromFile(sv.worldmodel->entities);

    sv.active = true;

    // all setup is completed, any further precache statements are errors
    sv.state = ss_active;

    // run two frames to allow everything to settle
    host_frametime = 0.1;
    SV_Physics();
    SV_Physics();

    // create a baseline for more efficient communications
    SV_CreateBaseline();

    // send serverinfo to all connected clients
    for (i = 0, host_client = svs.clients; i < svs.maxclients;
        i++, host_client++) {
        if (host_client->active) {
            SV_SendServerinfo(host_client);
        }
    }

    Con_DPrintf("Server spawned.\n");
}

//============================================================================
// sv_phys.cpp contents
//============================================================================

/*
pushmove objects do not obey gravity, and do not interact with each other or trigger fields, but block normal movement and push normal objects when they move.

onground is set for toss objects when they come to a complete rest.  it is set for steping or walking objects

doors, plats, etc are SOLID_BSP, and MOVETYPE_PUSH
bonus items are SOLID_TRIGGER touch, and MOVETYPE_TOSS
corpses are SOLID_NOT and MOVETYPE_TOSS
crates are SOLID_BBOX and MOVETYPE_TOSS
walking monsters are SOLID_SLIDEBOX and MOVETYPE_STEP
flying/floating monsters are SOLID_SLIDEBOX and MOVETYPE_FLY

solid_edge items only clip against bsp models.

*/

#define MOVE_EPSILON 0.01

void SV_Physics_Toss(edict_t* ent);

/*
================
SV_CheckVelocity
================
*/
void SV_CheckVelocity(edict_t* ent)
{
    int i;

    //
    // bound velocity
    //
    for (i = 0; i < 3; i++) {
        if (IS_NAN(ent->v.velocity[i])) {
            Con_Printf("Got a NaN velocity on %s\n", PR_GetString(ent->v.classname));
            ent->v.velocity[i] = 0;
        }

        if (IS_NAN(ent->v.origin[i])) {
            Con_Printf("Got a NaN origin on %s\n", PR_GetString(ent->v.classname));
            ent->v.origin[i] = 0;
        }

        if (ent->v.velocity[i] > sv_maxvelocity.value) {
            ent->v.velocity[i] = sv_maxvelocity.value;
        } else if (ent->v.velocity[i] < -sv_maxvelocity.value) {
            ent->v.velocity[i] = -sv_maxvelocity.value;
        }
    }
}

/*
=============
SV_RunThink

Runs thinking code if time.  There is some play in the exact time the think
function will be called, because it is called before any movement is done
in a frame.  Not used for pushmove objects, because they must be exact.
Returns false if the entity removed itself.
=============
*/
qboolean SV_RunThink(edict_t* ent)
{
    float thinktime;

    thinktime = ent->v.nextthink;
    if (thinktime <= 0 || thinktime > sv.time + host_frametime) {
        return true;
    }

    if (thinktime < sv.time) {
        thinktime = sv.time; // don't let things stay in the past.
    }

    // it is possible to start that way
    // by a trigger with a local time.
    ent->v.nextthink = 0;
    pr_global_struct->time = thinktime;
    pr_global_struct->self = EDICT_TO_PROG(ent);
    pr_global_struct->other = EDICT_TO_PROG(sv.edicts);
    PR_ExecuteProgram(ent->v.think);

    return !ent->free;
}

/*
==================
SV_Impact

Two entities have touched, so run their touch functions
==================
*/
void SV_Impact(edict_t* e1, edict_t* e2)
{
    int old_self, old_other;

    old_self = pr_global_struct->self;
    old_other = pr_global_struct->other;

    pr_global_struct->time = sv.time;
    if (e1->v.touch && e1->v.solid != SOLID_NOT) {
        pr_global_struct->self = EDICT_TO_PROG(e1);
        pr_global_struct->other = EDICT_TO_PROG(e2);
        PR_ExecuteProgram(e1->v.touch);
    }

    if (e2->v.touch && e2->v.solid != SOLID_NOT) {
        pr_global_struct->self = EDICT_TO_PROG(e2);
        pr_global_struct->other = EDICT_TO_PROG(e1);
        PR_ExecuteProgram(e2->v.touch);
    }

    pr_global_struct->self = old_self;
    pr_global_struct->other = old_other;
}

/*
==================
ClipVelocity

Slide off of the impacting object
returns the blocked flags (1 = floor, 2 = step / wall)
==================
*/
#define STOP_EPSILON 0.1

int ClipVelocity(vec3_t in, vec3_t normal, vec3_t out, float overbounce)
{
    float backoff;
    float change;
    int i, blocked;

    blocked = 0;
    if (normal[2] > 0) {
        blocked |= 1; // floor
    }

    if (!normal[2]) {
        blocked |= 2; // step
    }

    backoff = DotProduct(in, normal) * overbounce;

    for (i = 0; i < 3; i++) {
        change = normal[i] * backoff;
        out[i] = in[i] - change;
        if (out[i] > -STOP_EPSILON && out[i] < STOP_EPSILON) {
            out[i] = 0;
        }
    }

    return blocked;
}

/*
============
SV_FlyMove

The basic solid body movement clip that slides along multiple planes
Returns the clipflags if the velocity was modified (hit something solid)
1 = floor
2 = wall / step
4 = dead stop
If steptrace is not NULL, the trace of any vertical wall hit will be stored
============
*/
#define MAX_CLIP_PLANES 5

int SV_FlyMove(edict_t* ent, float time, trace_t* steptrace)
{
    int bumpcount, numbumps;
    vec3_t dir;
    float d;
    int numplanes;
    vec3_t planes[MAX_CLIP_PLANES];
    vec3_t primal_velocity, original_velocity, new_velocity;
    int i, j;
    trace_t trace;
    vec3_t end;
    float time_left;
    int blocked;

    numbumps = 4;

    blocked = 0;
    VectorCopy(ent->v.velocity, original_velocity);
    VectorCopy(ent->v.velocity, primal_velocity);
    numplanes = 0;

    time_left = time;

    for (bumpcount = 0; bumpcount < numbumps; bumpcount++) {
        if (!ent->v.velocity[0] && !ent->v.velocity[1] && !ent->v.velocity[2]) {
            break;
        }

        for (i = 0; i < 3; i++) {
            end[i] = ent->v.origin[i] + time_left * ent->v.velocity[i];
        }

        trace = SV_Move(ent->v.origin, ent->v.mins, ent->v.maxs, end, false, ent);

        if (trace.allsolid) { // entity is trapped in another solid
            VectorCopy(vec3_origin, ent->v.velocity);

            return 3;
        }

        if (trace.fraction > 0) { // actually covered some distance
            VectorCopy(trace.endpos, ent->v.origin);
            VectorCopy(ent->v.velocity, original_velocity);
            numplanes = 0;
        }

        if (trace.fraction == 1) {
            break; // moved the entire distance
        }

        if (!trace.ent) {
            Sys_Error("SV_FlyMove: !trace.ent");
        }

        if (trace.plane.normal[2] > 0.7) {
            blocked |= 1; // floor
            if (trace.ent->v.solid == SOLID_BSP) {
                ent->v.flags = (int)ent->v.flags | FL_ONGROUND;
                ent->v.groundentity = EDICT_TO_PROG(trace.ent);
            }
        }

        if (!trace.plane.normal[2]) {
            blocked |= 2; // step
            if (steptrace) {
                *steptrace = trace; // save for player extrafriction
            }
        }

        //
        // run the impact function
        //
        SV_Impact(ent, trace.ent);
        if (ent->free) {
            break; // removed by the impact function
        }

        time_left -= time_left * trace.fraction;

        // cliped to another plane
        if (numplanes >= MAX_CLIP_PLANES) { // this shouldn't really happen
            VectorCopy(vec3_origin, ent->v.velocity);

            return 3;
        }

        VectorCopy(trace.plane.normal, planes[numplanes]);
        numplanes++;

        //
        // modify original_velocity so it parallels all of the clip planes
        //
        for (i = 0; i < numplanes; i++) {
            ClipVelocity(original_velocity, planes[i], new_velocity, 1);
            for (j = 0; j < numplanes; j++) {
                if (j != i) {
                    if (DotProduct(new_velocity, planes[j]) < 0) {
                        break; // not ok
                    }
                }
            }
            if (j == numplanes) {
                break;
            }
        }

        if (i != numplanes) { // go along this plane
            VectorCopy(new_velocity, ent->v.velocity);
        } else { // go along the crease
            if (numplanes != 2) {
                //				Con_Printf ("clip velocity, numplanes == %i\n",numplanes);
                VectorCopy(vec3_origin, ent->v.velocity);

                return 7;
            }

            CrossProduct(planes[0], planes[1], dir);
            d = DotProduct(dir, ent->v.velocity);
            VectorScale(dir, d, ent->v.velocity);
        }

        //
        // if original velocity is against the original velocity, stop dead
        // to avoid tiny occilations in sloping corners
        //
        if (DotProduct(ent->v.velocity, primal_velocity) <= 0) {
            VectorCopy(vec3_origin, ent->v.velocity);

            return blocked;
        }
    }

    return blocked;
}

/*
============
SV_AddGravity

============
*/
void SV_AddGravity(edict_t* ent)
{
    float ent_gravity;

    eval_t* val;

    val = GetEdictFieldValue(ent, "gravity");
    if (val && val->_float) {
        ent_gravity = val->_float;
    } else {
        ent_gravity = 1.0;
    }

    ent->v.velocity[2] -= ent_gravity * sv_gravity.value * host_frametime;
}

//=============================================================================
// PUSHMOVE
//=============================================================================

/*
============
SV_PushEntity

Does not change the entities velocity at all
============
*/
trace_t SV_PushEntity(edict_t* ent, vec3_t push)
{
    trace_t trace;
    vec3_t end;

    VectorAdd(ent->v.origin, push, end);

    if (ent->v.movetype == MOVETYPE_FLYMISSILE) {
        trace = SV_Move(ent->v.origin, ent->v.mins, ent->v.maxs, end, MOVE_MISSILE,
            ent);
    } else if (ent->v.solid == SOLID_TRIGGER || ent->v.solid == SOLID_NOT) {
        // only clip against bmodels
        trace = SV_Move(ent->v.origin, ent->v.mins, ent->v.maxs, end,
            MOVE_NOMONSTERS, ent);
    } else {
        trace = SV_Move(ent->v.origin, ent->v.mins, ent->v.maxs, end, MOVE_NORMAL, ent);
    }

    VectorCopy(trace.endpos, ent->v.origin);
    SV_LinkEdict(ent, true);

    if (trace.ent) {
        SV_Impact(ent, trace.ent);
    }

    return trace;
}

/*
============
SV_PushMove

============
*/
void SV_PushMove(edict_t* pusher, float movetime)
{
    int i, e;
    edict_t *check, *block;
    vec3_t mins, maxs, move;
    vec3_t entorig, pushorig;
    int num_moved;
    edict_t* moved_edict[MAX_EDICTS];
    vec3_t moved_from[MAX_EDICTS];

    if (!pusher->v.velocity[0] && !pusher->v.velocity[1] && !pusher->v.velocity[2]) {
        pusher->v.ltime += movetime;

        return;
    }

    for (i = 0; i < 3; i++) {
        move[i] = pusher->v.velocity[i] * movetime;
        mins[i] = pusher->v.absmin[i] + move[i];
        maxs[i] = pusher->v.absmax[i] + move[i];
    }

    VectorCopy(pusher->v.origin, pushorig);

    // move the pusher to it's final position

    VectorAdd(pusher->v.origin, move, pusher->v.origin);
    pusher->v.ltime += movetime;
    SV_LinkEdict(pusher, false);

    // see if any solid entities are inside the final position
    num_moved = 0;
    check = NEXT_EDICT(sv.edicts);
    for (e = 1; e < sv.num_edicts; e++, check = NEXT_EDICT(check)) {
        if (check->free) {
            continue;
        }

        if (check->v.movetype == MOVETYPE_PUSH || check->v.movetype == MOVETYPE_NONE
            || check->v.movetype == MOVETYPE_NOCLIP) {
            continue;
        }

        // if the entity is standing on the pusher, it will definately be moved
        if (!(((int)check->v.flags & FL_ONGROUND) && PROG_TO_EDICT(check->v.groundentity) == pusher)) {
            if (check->v.absmin[0] >= maxs[0] || check->v.absmin[1] >= maxs[1] || check->v.absmin[2] >= maxs[2] || check->v.absmax[0] <= mins[0] || check->v.absmax[1] <= mins[1] || check->v.absmax[2] <= mins[2]) {
                continue;
            }

            // see if the ent's bbox is inside the pusher's final position
            if (!SV_TestEntityPosition(check)) {
                continue;
            }
        }

        // remove the onground flag for non-players
        if (check->v.movetype != MOVETYPE_WALK) {
            check->v.flags = (int)check->v.flags & ~FL_ONGROUND;
        }

        VectorCopy(check->v.origin, entorig);
        VectorCopy(check->v.origin, moved_from[num_moved]);
        moved_edict[num_moved] = check;
        num_moved++;

        // try moving the contacted entity
        pusher->v.solid = SOLID_NOT;
        SV_PushEntity(check, move);
        pusher->v.solid = SOLID_BSP;

        // if it is still inside the pusher, block
        block = SV_TestEntityPosition(check);
        if (block) { // fail the move
            if (check->v.mins[0] == check->v.maxs[0]) {
                continue;
            }

            if (check->v.solid == SOLID_NOT || check->v.solid == SOLID_TRIGGER) { // corpse
                check->v.mins[0] = check->v.mins[1] = 0;
                VectorCopy(check->v.mins, check->v.maxs);
                continue;
            }

            VectorCopy(entorig, check->v.origin);
            SV_LinkEdict(check, true);

            VectorCopy(pushorig, pusher->v.origin);
            SV_LinkEdict(pusher, false);
            pusher->v.ltime -= movetime;

            // if the pusher has a "blocked" function, call it
            // otherwise, just stay in place until the obstacle is gone
            if (pusher->v.blocked) {
                pr_global_struct->self = EDICT_TO_PROG(pusher);
                pr_global_struct->other = EDICT_TO_PROG(check);
                PR_ExecuteProgram(pusher->v.blocked);
            }

            // move back any entities we already moved
            for (i = 0; i < num_moved; i++) {
                VectorCopy(moved_from[i], moved_edict[i]->v.origin);
                SV_LinkEdict(moved_edict[i], false);
            }

            return;
        }
    }
}


/*
================
SV_Physics_Pusher

================
*/
void SV_Physics_Pusher(edict_t* ent)
{
    float thinktime;
    float oldltime;
    float movetime;

    oldltime = ent->v.ltime;

    thinktime = ent->v.nextthink;
    if (thinktime < ent->v.ltime + host_frametime) {
        movetime = thinktime - ent->v.ltime;
        if (movetime < 0) {
            movetime = 0;
        }
    } else {
        movetime = host_frametime;
    }

    if (movetime) {
            SV_PushMove(ent, movetime); // advances ent->v.ltime if not blocked
    }

    if (thinktime > oldltime && thinktime <= ent->v.ltime) {
        ent->v.nextthink = 0;
        pr_global_struct->time = sv.time;
        pr_global_struct->self = EDICT_TO_PROG(ent);
        pr_global_struct->other = EDICT_TO_PROG(sv.edicts);
        PR_ExecuteProgram(ent->v.think);
        if (ent->free) {
            return;
        }
    }
}

//=============================================================================
// CLIENT MOVEMENT
//=============================================================================

/*
=============
SV_CheckStuck

This is a big hack to try and fix the rare case of getting stuck in the world
clipping hull.
=============
*/
void SV_CheckStuck(edict_t* ent)
{
    int i, j;
    int z;
    vec3_t org;

    if (!SV_TestEntityPosition(ent)) {
        VectorCopy(ent->v.origin, ent->v.oldorigin);

        return;
    }

    VectorCopy(ent->v.origin, org);
    VectorCopy(ent->v.oldorigin, ent->v.origin);
    if (!SV_TestEntityPosition(ent)) {
        Con_DPrintf("Unstuck.\n");
        SV_LinkEdict(ent, true);

        return;
    }

    for (z = 0; z < 18; z++) {
        for (i = -1; i <= 1; i++) {
            for (j = -1; j <= 1; j++) {
                ent->v.origin[0] = org[0] + i;
                ent->v.origin[1] = org[1] + j;
                ent->v.origin[2] = org[2] + z;
                if (!SV_TestEntityPosition(ent)) {
                    Con_DPrintf("Unstuck.\n");
                    SV_LinkEdict(ent, true);

                    return;
                }
            }
        }
    }

    VectorCopy(org, ent->v.origin);
    Con_DPrintf("player is stuck.\n");
}

/*
=============
SV_CheckWater
=============
*/
qboolean SV_CheckWater(edict_t* ent)
{
    vec3_t point;
    int cont;

    point[0] = ent->v.origin[0];
    point[1] = ent->v.origin[1];
    point[2] = ent->v.origin[2] + ent->v.mins[2] + 1;

    ent->v.waterlevel = 0;
    ent->v.watertype = CONTENTS_EMPTY;
    cont = SV_PointContents(point);
    if (cont <= CONTENTS_WATER) {
        ent->v.watertype = cont;
        ent->v.waterlevel = 1;
        point[2] = ent->v.origin[2] + (ent->v.mins[2] + ent->v.maxs[2]) * 0.5;
        cont = SV_PointContents(point);
        if (cont <= CONTENTS_WATER) {
            ent->v.waterlevel = 2;
            point[2] = ent->v.origin[2] + ent->v.view_ofs[2];
            cont = SV_PointContents(point);
            if (cont <= CONTENTS_WATER) {
                ent->v.waterlevel = 3;
            }
        }

    }

    return ent->v.waterlevel > 1;
}

/*
============
SV_WallFriction

============
*/
void SV_WallFriction(edict_t* ent, trace_t* trace)
{
    vec3_t forward, right, up;
    float d, i;
    vec3_t into, side;

    AngleVectors(ent->v.v_angle, forward, right, up);
    d = DotProduct(trace->plane.normal, forward);

    d += 0.5;
    if (d >= 0) {
        return;
    }

    // cut the tangential velocity
    i = DotProduct(trace->plane.normal, ent->v.velocity);
    VectorScale(trace->plane.normal, i, into);
    VectorSubtract(ent->v.velocity, into, side);

    ent->v.velocity[0] = side[0] * (1 + d);
    ent->v.velocity[1] = side[1] * (1 + d);
}

/*
=====================
SV_TryUnstick

Player has come to a dead stop, possibly due to the problem with limited
float precision at some angle joins in the BSP hull.

Try fixing by pushing one pixel in each direction.

This is a hack, but in the interest of good gameplay...
======================
*/
int SV_TryUnstick(edict_t* ent, vec3_t oldvel)
{
    int i;
    vec3_t oldorg;
    vec3_t dir;
    int clip;
    trace_t steptrace;

    VectorCopy(ent->v.origin, oldorg);
    VectorCopy(vec3_origin, dir);

    for (i = 0; i < 8; i++) {
        // try pushing a little in an axial direction
        switch (i) {
        case 0:
            dir[0] = 2;
            dir[1] = 0;
            break;
        case 1:
            dir[0] = 0;
            dir[1] = 2;
            break;
        case 2:
            dir[0] = -2;
            dir[1] = 0;
            break;
        case 3:
            dir[0] = 0;
            dir[1] = -2;
            break;
        case 4:
            dir[0] = 2;
            dir[1] = 2;
            break;
        case 5:
            dir[0] = -2;
            dir[1] = 2;
            break;
        case 6:
            dir[0] = 2;
            dir[1] = -2;
            break;
        case 7:
            dir[0] = -2;
            dir[1] = -2;
            break;
        }

        SV_PushEntity(ent, dir);

        // retry the original move
        ent->v.velocity[0] = oldvel[0];
        ent->v.velocity[1] = oldvel[1];
        ent->v.velocity[2] = 0;
        clip = SV_FlyMove(ent, 0.1f, &steptrace);

        if (fabs(oldorg[1] - ent->v.origin[1]) > 4 || fabs(oldorg[0] - ent->v.origin[0]) > 4) {
            //Con_DPrintf ("unstuck!\n");
            return clip;
        }

        // go back to the original pos and try again
        VectorCopy(oldorg, ent->v.origin);
    }

    VectorCopy(vec3_origin, ent->v.velocity);

    return 7; // still not moving
}

/*
=====================
SV_WalkMove

Only used by players
======================
*/
#define STEPSIZE 18

void SV_WalkMove(edict_t* ent)
{
    vec3_t upmove, downmove;
    vec3_t oldorg, oldvel;
    vec3_t nosteporg, nostepvel;
    int clip;
    int oldonground;
    trace_t steptrace, downtrace;

    //
    // do a regular slide move unless it looks like you ran into a step
    //
    oldonground = (int)ent->v.flags & FL_ONGROUND;
    ent->v.flags = (int)ent->v.flags & ~FL_ONGROUND;

    VectorCopy(ent->v.origin, oldorg);
    VectorCopy(ent->v.velocity, oldvel);

    clip = SV_FlyMove(ent, host_frametime, &steptrace);

    if (!(clip & 2)) {
        return; // move didn't block on a step
    }

    if (!oldonground && ent->v.waterlevel == 0) {
        return; // don't stair up while jumping
    }

    if (ent->v.movetype != MOVETYPE_WALK) {
        return; // gibbed by a trigger
    }

    if (sv_nostep.value) {
        return;
    }

    if ((int)sv_player->v.flags & FL_WATERJUMP) {
        return;
    }

    VectorCopy(ent->v.origin, nosteporg);
    VectorCopy(ent->v.velocity, nostepvel);

    //
    // try moving up and forward to go up a step
    //
    VectorCopy(oldorg, ent->v.origin); // back to start pos

    VectorCopy(vec3_origin, upmove);
    VectorCopy(vec3_origin, downmove);
    upmove[2] = STEPSIZE;
    downmove[2] = -STEPSIZE + oldvel[2] * host_frametime;

    // move up
    SV_PushEntity(ent, upmove); // FIXME: don't link?

    // move forward
    ent->v.velocity[0] = oldvel[0];
    ent->v.velocity[1] = oldvel[1];
    ent->v.velocity[2] = 0;
    clip = SV_FlyMove(ent, host_frametime, &steptrace);

    // check for stuckness, possibly due to the limited precision of floats
    // in the clipping hulls
    if (clip) {
        if (fabs(oldorg[1] - ent->v.origin[1]) < 0.03125 && fabs(oldorg[0] - ent->v.origin[0]) < 0.03125) { // stepping up didn't make any progress
            clip = SV_TryUnstick(ent, oldvel);
        }
    }

    // extra friction based on view angle
    if (clip & 2) {
        SV_WallFriction(ent, &steptrace);
    }

    // move down
    downtrace = SV_PushEntity(ent, downmove); // FIXME: don't link?

    if (downtrace.plane.normal[2] > 0.7) {
        if (ent->v.solid == SOLID_BSP) {
            ent->v.flags = (int)ent->v.flags | FL_ONGROUND;
            ent->v.groundentity = EDICT_TO_PROG(downtrace.ent);
        }
    } else {
        // if the push down didn't end up on good ground, use the move without
        // the step up.  This happens near wall / slope combinations, and can
        // cause the player to hop up higher on a slope too steep to climb
        VectorCopy(nosteporg, ent->v.origin);
        VectorCopy(nostepvel, ent->v.velocity);
    }
}

/*
================
SV_Physics_Client

Player character actions
================
*/
void SV_Physics_Client(edict_t* ent, int num)
{
    if (!svs.clients[num - 1].active) {
        return; // unconnected slot
    }

    //
    // call standard client pre-think
    //
    pr_global_struct->time = sv.time;
    pr_global_struct->self = EDICT_TO_PROG(ent);
    PR_ExecuteProgram(pr_global_struct->PlayerPreThink);

    //
    // do a move
    //
    SV_CheckVelocity(ent);

    //
    // decide which move function to call
    //
    switch ((int)ent->v.movetype) {
    case MOVETYPE_NONE:
        if (!SV_RunThink(ent)) {
            return;
        }

        break;

    case MOVETYPE_WALK:
        if (!SV_RunThink(ent)) {
            return;
        }

        if (!SV_CheckWater(ent) && !((int)ent->v.flags & FL_WATERJUMP)) {
            SV_AddGravity(ent);
        }

        SV_CheckStuck(ent);
        SV_WalkMove(ent);

        break;

    case MOVETYPE_TOSS:
    case MOVETYPE_BOUNCE:
        SV_Physics_Toss(ent);
        break;

    case MOVETYPE_FLY:
        if (!SV_RunThink(ent)) {
            return;
        }

        SV_FlyMove(ent, host_frametime, NULL);
        break;

    case MOVETYPE_NOCLIP:
        if (!SV_RunThink(ent)) {
            return;
        }

        VectorMA(ent->v.origin, host_frametime, ent->v.velocity, ent->v.origin);
        break;

    default:
        Sys_Error("SV_Physics_client: bad movetype %i", (int)ent->v.movetype);
    }

    //
    // call standard player post-think
    //
    SV_LinkEdict(ent, true);

    pr_global_struct->time = sv.time;
    pr_global_struct->self = EDICT_TO_PROG(ent);
    PR_ExecuteProgram(pr_global_struct->PlayerPostThink);
}

//============================================================================

/*
=============
SV_Physics_None

Non moving objects can only think
=============
*/
void SV_Physics_None(edict_t* ent)
{
    // regular thinking
    SV_RunThink(ent);
}


/*
=============
SV_Physics_Noclip

A moving object that doesn't obey physics
=============
*/
void SV_Physics_Noclip(edict_t* ent)
{
    // regular thinking
    if (!SV_RunThink(ent)) {
        return;
    }

    VectorMA(ent->v.angles, host_frametime, ent->v.avelocity, ent->v.angles);
    VectorMA(ent->v.origin, host_frametime, ent->v.velocity, ent->v.origin);

    SV_LinkEdict(ent, false);
}

//==============================================================================
// TOSS / BOUNCE
//==============================================================================

/*
=============
SV_CheckWaterTransition

=============
*/
void SV_CheckWaterTransition(edict_t* ent)
{
    int cont;
    cont = SV_PointContents(ent->v.origin);
    if (!ent->v.watertype) { // just spawned here
        ent->v.watertype = cont;
        ent->v.waterlevel = 1;

        return;
    }

    if (cont <= CONTENTS_WATER) {
        if (ent->v.watertype == CONTENTS_EMPTY) { // just crossed into water
            SV_StartSound(ent, 0, "misc/h2ohit1.wav", 255, 1);
        }

        ent->v.watertype = cont;
        ent->v.waterlevel = 1;
    } else {
        if (ent->v.watertype != CONTENTS_EMPTY) { // just crossed into water
            SV_StartSound(ent, 0, "misc/h2ohit1.wav", 255, 1);
        }

        ent->v.watertype = CONTENTS_EMPTY;
        ent->v.waterlevel = cont;
    }
}

/*
=============
SV_Physics_Toss

Toss, bounce, and fly movement.  When onground, do nothing.
=============
*/
void SV_Physics_Toss(edict_t* ent)
{
    trace_t trace;
    vec3_t move;
    float backoff;
    // regular thinking
    if (!SV_RunThink(ent)) {
        return;
    }

    // if onground, return without moving
    if (((int)ent->v.flags & FL_ONGROUND)) {
        return;
    }

    SV_CheckVelocity(ent);

    // add gravity
    if (ent->v.movetype != MOVETYPE_FLY && ent->v.movetype != MOVETYPE_FLYMISSILE) {
        SV_AddGravity(ent);
    }


    // move angles
    VectorMA(ent->v.angles, host_frametime, ent->v.avelocity, ent->v.angles);

// move origin
    VectorScale(ent->v.velocity, host_frametime, move);
    trace = SV_PushEntity(ent, move);
    if (trace.fraction == 1) {
        return;
    }

    if (ent->free) {
        return;
    }

    if (ent->v.movetype == MOVETYPE_BOUNCE) {
        backoff = 1.5;
    }

    else {
        backoff = 1;
    }

    ClipVelocity(ent->v.velocity, trace.plane.normal, ent->v.velocity, backoff);

    // stop if on ground
    if (trace.plane.normal[2] > 0.7) {
        if (ent->v.velocity[2] < 60 || ent->v.movetype != MOVETYPE_BOUNCE)
        {
            ent->v.flags = (int)ent->v.flags | FL_ONGROUND;
            ent->v.groundentity = EDICT_TO_PROG(trace.ent);
            VectorCopy(vec3_origin, ent->v.velocity);
            VectorCopy(vec3_origin, ent->v.avelocity);
        }
    }

    // check for in water
    SV_CheckWaterTransition(ent);
}

//===============================================================================
// STEPPING MOVEMENT
//===============================================================================

/*
=============
SV_Physics_Step

Monsters freefall when they don't have a ground entity, otherwise
all movement is done with discrete steps.

This is also used for objects that have become still on the ground, but
will fall if the floor is pulled out from under them.
=============
*/
void SV_Physics_Step(edict_t* ent)
{
    qboolean hitsound;

    // freefall if not onground
    if (!((int)ent->v.flags & (FL_ONGROUND | FL_FLY | FL_SWIM))) {
        if (ent->v.velocity[2] < sv_gravity.value * -0.1) {
            hitsound = true;
        } else {
            hitsound = false;
        }

        SV_AddGravity(ent);
        SV_CheckVelocity(ent);
        SV_FlyMove(ent, host_frametime, NULL);
        SV_LinkEdict(ent, true);

        if ((int)ent->v.flags & FL_ONGROUND) // just hit ground
        {
            if (hitsound) {
                SV_StartSound(ent, 0, "demon/dland2.wav", 255, 1);
            }
        }
    }

    // regular thinking
    SV_RunThink(ent);

    SV_CheckWaterTransition(ent);
}

//============================================================================

/*
================
SV_Physics

================
*/
void SV_Physics(void)
{
    int i;
    edict_t* ent;

    // let the progs know that a new frame has started
    pr_global_struct->self = EDICT_TO_PROG(sv.edicts);
    pr_global_struct->other = EDICT_TO_PROG(sv.edicts);
    pr_global_struct->time = sv.time;
    PR_ExecuteProgram(pr_global_struct->StartFrame);

    //SV_CheckAllEnts ();

    //
    // treat each object in turn
    //
    ent = sv.edicts;
    for (i = 0; i < sv.num_edicts; i++, ent = NEXT_EDICT(ent)) {
        if (ent->free) {
            continue;
        }

        if (pr_global_struct->force_retouch) {
            SV_LinkEdict(ent, true); // force retouch even for stationary
        }

        if (i > 0 && i <= svs.maxclients) {
            SV_Physics_Client(ent, i);
        } else if (ent->v.movetype == MOVETYPE_PUSH) {
            SV_Physics_Pusher(ent);
        } else if (ent->v.movetype == MOVETYPE_NONE) {
            SV_Physics_None(ent);
        }

        else if (ent->v.movetype == MOVETYPE_NOCLIP) {
            SV_Physics_Noclip(ent);
        } else if (ent->v.movetype == MOVETYPE_STEP) {
            SV_Physics_Step(ent);
        } else if (ent->v.movetype == MOVETYPE_TOSS || ent->v.movetype == MOVETYPE_BOUNCE
            || ent->v.movetype == MOVETYPE_FLY || ent->v.movetype == MOVETYPE_FLYMISSILE) {
            SV_Physics_Toss(ent);
        } else {
            Sys_Error("SV_Physics: bad movetype %i", (int)ent->v.movetype);
        }
    }

    if (pr_global_struct->force_retouch) {
        pr_global_struct->force_retouch--;
    }

    sv.time += host_frametime;
}

//============================================================================
// sv_move.cpp contents
//============================================================================

/*
=============
SV_CheckBottom

Returns false if any part of the bottom of the entity is off an edge that
is not a staircase.

=============
*/
qboolean SV_CheckBottom(edict_t* ent)
{
    vec3_t mins, maxs, start, stop;
    trace_t trace;
    int x, y;
    float mid, bottom;

    VectorAdd(ent->v.origin, ent->v.mins, mins);
    VectorAdd(ent->v.origin, ent->v.maxs, maxs);

    // if all of the points under the corners are solid world, don't bother
    // with the tougher checks
    // the corners must be within 16 of the midpoint
    start[2] = mins[2] - 1;
    for (x = 0; x <= 1; x++) {
        for (y = 0; y <= 1; y++) {
            start[0] = x ? maxs[0] : mins[0];
            start[1] = y ? maxs[1] : mins[1];
            if (SV_PointContents(start) != CONTENTS_SOLID) {
                goto realcheck;
            }
        }
    }

    c_yes++;

    return true; // we got out easy

realcheck:
    c_no++;
    //
    // check it for real...
    //
    start[2] = mins[2];

    // the midpoint must be within 16 of the bottom
    start[0] = stop[0] = (mins[0] + maxs[0]) * 0.5;
    start[1] = stop[1] = (mins[1] + maxs[1]) * 0.5;
    stop[2] = start[2] - 2 * STEPSIZE;
    trace = SV_Move(start, vec3_origin, vec3_origin, stop, true, ent);

    if (trace.fraction == 1.0) {
        return false;
    }

    mid = bottom = trace.endpos[2];

    // the corners must be within 16 of the midpoint
    for (x = 0; x <= 1; x++) {
        for (y = 0; y <= 1; y++) {
            start[0] = stop[0] = x ? maxs[0] : mins[0];
            start[1] = stop[1] = y ? maxs[1] : mins[1];

            trace = SV_Move(start, vec3_origin, vec3_origin, stop, true, ent);

            if (trace.fraction != 1.0 && trace.endpos[2] > bottom) {
                bottom = trace.endpos[2];
            }

            if (trace.fraction == 1.0 || mid - trace.endpos[2] > STEPSIZE) {
                return false;
            }
        }
    }

    c_yes++;

    return true;
}

/*
=============
SV_movestep

Called by monster program code.
The move will be adjusted for slopes and stairs, but if the move isn't
possible, no move is done, false is returned, and
pr_global_struct->trace_normal is set to the normal of the blocking wall
=============
*/
qboolean SV_movestep(edict_t* ent, vec3_t move, qboolean relink)
{
    float dz;
    vec3_t oldorg, neworg, end;
    trace_t trace;
    int i;
    edict_t* enemy;

    // try the move
    VectorCopy(ent->v.origin, oldorg);
    VectorAdd(ent->v.origin, move, neworg);

    // flying monsters don't step up
    if ((int)ent->v.flags & (FL_SWIM | FL_FLY)) {
        // try one move with vertical motion, then one without
        for (i = 0; i < 2; i++) {
            VectorAdd(ent->v.origin, move, neworg);
            enemy = PROG_TO_EDICT(ent->v.enemy);
            if (i == 0 && enemy != sv.edicts) {
                dz = ent->v.origin[2] - PROG_TO_EDICT(ent->v.enemy)->v.origin[2];
                if (dz > 40) {
                    neworg[2] -= 8;
                }

                if (dz < 30) {
                    neworg[2] += 8;
                }
            }

            trace = SV_Move(ent->v.origin, ent->v.mins, ent->v.maxs, neworg, false, ent);

            if (trace.fraction == 1) {
                if (((int)ent->v.flags & FL_SWIM) && SV_PointContents(trace.endpos) == CONTENTS_EMPTY) {
                    return false; // swim monster left water
                }

                VectorCopy(trace.endpos, ent->v.origin);
                if (relink) {
                    SV_LinkEdict(ent, true);
                }

                return true;
            }

            if (enemy == sv.edicts) {
                break;
            }
        }

        return false;
    }

    // push down from a step height above the wished position
    neworg[2] += STEPSIZE;
    VectorCopy(neworg, end);
    end[2] -= STEPSIZE * 2;

    trace = SV_Move(neworg, ent->v.mins, ent->v.maxs, end, false, ent);

    if (trace.allsolid) {
        return false;
    }

    if (trace.startsolid) {
        neworg[2] -= STEPSIZE;
        trace = SV_Move(neworg, ent->v.mins, ent->v.maxs, end, false, ent);
        if (trace.allsolid || trace.startsolid) {
            return false;
        }
    }

    if (trace.fraction == 1) {
        // if monster had the ground pulled out, go ahead and fall
        if ((int)ent->v.flags & FL_PARTIALGROUND) {
            VectorAdd(ent->v.origin, move, ent->v.origin);
            if (relink) {
                SV_LinkEdict(ent, true);
            }

            ent->v.flags = (int)ent->v.flags & ~FL_ONGROUND;

            //	Con_Printf ("fall down\n");
            return true;
        }

        return false; // walked off an edge
    }

    // check point traces down for dangling corners
    VectorCopy(trace.endpos, ent->v.origin);

    if (!SV_CheckBottom(ent)) {
        if ((int)ent->v.flags & FL_PARTIALGROUND) { // entity had floor mostly pulled out from underneath it
            // and is trying to correct
            if (relink) {
                SV_LinkEdict(ent, true);
            }

            return true;
        }

        VectorCopy(oldorg, ent->v.origin);

        return false;
    }

    if ((int)ent->v.flags & FL_PARTIALGROUND) {
        //		Con_Printf ("back on ground\n");
        ent->v.flags = (int)ent->v.flags & ~FL_PARTIALGROUND;
    }

    ent->v.groundentity = EDICT_TO_PROG(trace.ent);

    // the move is ok
    if (relink) {
        SV_LinkEdict(ent, true);
    }

    return true;
}

//============================================================================

/*
======================
SV_StepDirection

Turns to the movement direction, and walks the current distance if
facing it.

======================
*/

namespace VM { void PF_changeyaw(void); }

qboolean SV_StepDirection(edict_t* ent, float yaw, float dist)
{
    vec3_t move, oldorigin;
    float delta;

    ent->v.ideal_yaw = yaw;
    PF_changeyaw();

    yaw = yaw * M_PI * 2 / 360;
    move[0] = cos(yaw) * dist;
    move[1] = sin(yaw) * dist;
    move[2] = 0;

    VectorCopy(ent->v.origin, oldorigin);
    if (SV_movestep(ent, move, false)) {
        delta = ent->v.angles[YAW] - ent->v.ideal_yaw;
        if (delta > 45 && delta < 315) { // not turned far enough, so don't take the step
            VectorCopy(oldorigin, ent->v.origin);
        }

        SV_LinkEdict(ent, true);

        return true;
    }

    SV_LinkEdict(ent, true);

    return false;
}

/*
======================
SV_FixCheckBottom

======================
*/
void SV_FixCheckBottom(edict_t* ent)
{
    //	Con_Printf ("SV_FixCheckBottom\n");

    ent->v.flags = (int)ent->v.flags | FL_PARTIALGROUND;
}

/*
================
SV_NewChaseDir

================
*/
#define DI_NODIR -1

void SV_NewChaseDir(edict_t* actor, edict_t* enemy, float dist)
{
    float deltax, deltay;
    float d[3];
    float tdir, olddir, turnaround;

    olddir = anglemod((int)(actor->v.ideal_yaw / 45) * 45);
    turnaround = anglemod(olddir - 180);

    deltax = enemy->v.origin[0] - actor->v.origin[0];
    deltay = enemy->v.origin[1] - actor->v.origin[1];
    if (deltax > 10) {
        d[1] = 0;
    } else if (deltax < -10) {
        d[1] = 180;
    } else {
        d[1] = DI_NODIR;
    }

    if (deltay < -10) {
        d[2] = 270;
    } else if (deltay > 10) {
        d[2] = 90;
    } else {
        d[2] = DI_NODIR;
    }

    // try direct route
    if (d[1] != DI_NODIR && d[2] != DI_NODIR) {
        if (d[1] == 0) {
            tdir = d[2] == 90 ? 45 : 315;
        } else {
            tdir = d[2] == 90 ? 135 : 215;
        }

        if (tdir != turnaround && SV_StepDirection(actor, tdir, dist)) {
            return;
        }
    }

    // try other directions
    if (((rand() & 3) & 1) || fabs(deltay) > fabs(deltax)) {
        tdir = d[1];
        d[1] = d[2];
        d[2] = tdir;
    }

    if (d[1] != DI_NODIR && d[1] != turnaround && SV_StepDirection(actor, d[1], dist)) {
        return;
    }

    if (d[2] != DI_NODIR && d[2] != turnaround && SV_StepDirection(actor, d[2], dist)) {
        return;
    }

    /* there is no direct path to the player, so pick another direction */

    if (olddir != DI_NODIR && SV_StepDirection(actor, olddir, dist)) {
        return;
    }

    if (rand() & 1) /*randomly determine direction of search*/
    {
        for (tdir = 0; tdir <= 315; tdir += 45) {
            if (tdir != turnaround && SV_StepDirection(actor, tdir, dist)) {
                return;
            }
        }
    } else {
        for (tdir = 315; tdir >= 0; tdir -= 45) {
            if (tdir != turnaround && SV_StepDirection(actor, tdir, dist)) {
                return;
            }
        }
    }

    if (turnaround != DI_NODIR && SV_StepDirection(actor, turnaround, dist)) {
        return;
    }

    actor->v.ideal_yaw = olddir; // can't move

    // if a bridge was pulled out from underneath a monster, it may not have
    // a valid standing position at all

    if (!SV_CheckBottom(actor)) {
        SV_FixCheckBottom(actor);
    }
}

/*
======================
SV_CloseEnough

======================
*/
qboolean SV_CloseEnough(edict_t* ent, edict_t* goal, float dist)
{
    int i;

    for (i = 0; i < 3; i++) {
        if (goal->v.absmin[i] > ent->v.absmax[i] + dist) {
            return false;
        }

        if (goal->v.absmax[i] < ent->v.absmin[i] - dist) {
            return false;
        }
    }

    return true;
}

/*
======================
SV_MoveToGoal

======================
*/
void SV_MoveToGoal(void)
{
    edict_t *ent, *goal;
    float dist;

    ent = PROG_TO_EDICT(pr_global_struct->self);
    goal = PROG_TO_EDICT(ent->v.goalentity);
    dist = G_FLOAT(OFS_PARM0);

    if (!((int)ent->v.flags & (FL_ONGROUND | FL_FLY | FL_SWIM))) {
        G_FLOAT(OFS_RETURN) = 0;

        return;
    }

// if the next step hits the enemy, return immediately
    if (PROG_TO_EDICT(ent->v.enemy) != sv.edicts && SV_CloseEnough(ent, goal, dist))
    {
        return;
    }

    // bump around...
    if ((rand() & 3) == 1 || !SV_StepDirection(ent, ent->v.ideal_yaw, dist)) {
        SV_NewChaseDir(ent, goal, dist);
    }
}

//============================================================================
// sv_user.cpp contents
//============================================================================

/*
===============
SV_SetIdealPitch
===============
*/
#define MAX_FORWARD 6

void SV_SetIdealPitch(void)
{
    float angleval, sinval, cosval;
    trace_t tr;
    vec3_t top, bottom;
    float z[MAX_FORWARD];
    int i, j;
    int step, dir, steps;

    if (!((int)sv_player->v.flags & FL_ONGROUND)) {
        return;
    }

    angleval = sv_player->v.angles[YAW] * M_PI * 2 / 360;
    sinval = sin(angleval);
    cosval = cos(angleval);

    for (i = 0; i < MAX_FORWARD; i++) {
        top[0] = sv_player->v.origin[0] + cosval * (i + 3) * 12;
        top[1] = sv_player->v.origin[1] + sinval * (i + 3) * 12;
        top[2] = sv_player->v.origin[2] + sv_player->v.view_ofs[2];

        bottom[0] = top[0];
        bottom[1] = top[1];
        bottom[2] = top[2] - 160;

        tr = SV_Move(top, vec3_origin, vec3_origin, bottom, 1, sv_player);
        if (tr.allsolid) {
            return; // looking at a wall, leave ideal the way is was
        }

        if (tr.fraction == 1) {
            return; // near a dropoff
        }

        z[i] = top[2] + tr.fraction * (bottom[2] - top[2]);
    }

    dir = 0;
    steps = 0;
    for (j = 1; j < i; j++) {
        step = z[j] - z[j - 1];
        if (step > -ON_EPSILON && step < ON_EPSILON) {
            continue;
        }

        if (dir && (step - dir > ON_EPSILON || step - dir < -ON_EPSILON)) {
            return; // mixed changes
        }

        steps++;
        dir = step;
    }

    if (!dir) {
        sv_player->v.idealpitch = 0;

        return;
    }

    if (steps < 2) {
        return;
    }

    sv_player->v.idealpitch = -dir * sv_idealpitchscale.value;
}

/*
==================
SV_UserFriction

==================
*/
void SV_UserFriction(void)
{
    float* vel;
    float speed, newspeed, control;
    vec3_t start, stop;
    float friction;
    trace_t trace;

    vel = velocity;

    speed = sqrt(vel[0] * vel[0] + vel[1] * vel[1]);
    if (!speed) {
        return;
    }

    // if the leading edge is over a dropoff, increase friction
    start[0] = stop[0] = origin[0] + vel[0] / speed * 16;
    start[1] = stop[1] = origin[1] + vel[1] / speed * 16;
    start[2] = origin[2] + sv_player->v.mins[2];
    stop[2] = start[2] - 34;

    trace = SV_Move(start, vec3_origin, vec3_origin, stop, true, sv_player);

    if (trace.fraction == 1.0) {
        friction = sv_friction.value * sv_edgefriction.value;
    } else {
        friction = sv_friction.value;
    }

    // apply friction
    control = speed < sv_stopspeed.value ? sv_stopspeed.value : speed;
    newspeed = speed - host_frametime * control * friction;

    if (newspeed < 0) {
        newspeed = 0;
    }

    newspeed /= speed;

    vel[0] = vel[0] * newspeed;
    vel[1] = vel[1] * newspeed;
    vel[2] = vel[2] * newspeed;
}

/*
==============
SV_Accelerate
==============
*/
void SV_Accelerate(void)
{
    int i;
    float addspeed, accelspeed, currentspeed;

    currentspeed = DotProduct(velocity, wishdir);
    addspeed = wishspeed - currentspeed;
    if (addspeed <= 0) {
        return;
    }

    accelspeed = sv_accelerate.value * host_frametime * wishspeed;
    if (accelspeed > addspeed) {
        accelspeed = addspeed;
    }

    for (i = 0; i < 3; i++) {
        velocity[i] += accelspeed * wishdir[i];
    }
}

void SV_AirAccelerate(vec3_t wishveloc)
{
    int i;
    float addspeed, wishspd, accelspeed, currentspeed;

    wishspd = VectorNormalize(wishveloc);
    if (wishspd > 30) {
        wishspd = 30;
    }

    currentspeed = DotProduct(velocity, wishveloc);
    addspeed = wishspd - currentspeed;
    if (addspeed <= 0) {
        return;
    }

    //	accelspeed = sv_accelerate.value * host_frametime;
    accelspeed = sv_accelerate.value * wishspeed * host_frametime;
    if (accelspeed > addspeed) {
        accelspeed = addspeed;
    }

    for (i = 0; i < 3; i++) {
        velocity[i] += accelspeed * wishveloc[i];
    }
}

void DropPunchAngle(void)
{
    float len;

    len = VectorNormalize(sv_player->v.punchangle);

    len -= 10 * host_frametime;
    if (len < 0) {
        len = 0;
    }

    VectorScale(sv_player->v.punchangle, len, sv_player->v.punchangle);
}

/*
===================
SV_WaterMove

===================
*/
void SV_WaterMove(void)
{
    int i;
    vec3_t wishvel;
    vec3_t forward, right, up;
    float speed, newspeed, w_speed, addspeed, accelspeed;

    //
    // user intentions
    //
    AngleVectors(sv_player->v.v_angle, forward, right, up);

    for (i = 0; i < 3; i++) {
        wishvel[i] = forward[i] * cmd.forwardmove + right[i] * cmd.sidemove;
    }

    if (!cmd.forwardmove && !cmd.sidemove && !cmd.upmove) {
        wishvel[2] -= 60; // drift towards bottom
    } else {
        wishvel[2] += cmd.upmove;
    }

    w_speed = Length(wishvel);
    if (w_speed > sv_maxspeed.value) {
        VectorScale(wishvel, sv_maxspeed.value / w_speed, wishvel);
        w_speed = sv_maxspeed.value;
    }

    w_speed *= 0.7f;

    //
    // water friction
    //
    speed = Length(velocity);
    if (speed) {
        newspeed = speed - host_frametime * speed * sv_friction.value;
        if (newspeed < 0) {
            newspeed = 0;
        }

        VectorScale(velocity, newspeed / speed, velocity);
    } else {
        newspeed = 0;
    }

    //
    // water acceleration
    //
    if (!w_speed) {
        return;
    }

    addspeed = w_speed - newspeed;
    if (addspeed <= 0) {
        return;
    }

    VectorNormalize(wishvel);
    accelspeed = sv_accelerate.value * w_speed * host_frametime;
    if (accelspeed > addspeed) {
        accelspeed = addspeed;
    }

    for (i = 0; i < 3; i++) {
        velocity[i] += accelspeed * wishvel[i];
    }
}

void SV_WaterJump(void)
{
    if (sv.time > sv_player->v.teleport_time || !sv_player->v.waterlevel) {
        sv_player->v.flags = (int)sv_player->v.flags & ~FL_WATERJUMP;
        sv_player->v.teleport_time = 0;
    }

    sv_player->v.velocity[0] = sv_player->v.movedir[0];
    sv_player->v.velocity[1] = sv_player->v.movedir[1];
}

/*
===================
SV_AirMove

===================
*/
void SV_AirMove(void)
{
    int i;
    vec3_t wishvel;
    vec3_t forward, right, up;
    float fmove, smove;

    AngleVectors(sv_player->v.angles, forward, right, up);

    fmove = cmd.forwardmove;
    smove = cmd.sidemove;

    // hack to not let you back into teleporter
    if (sv.time < sv_player->v.teleport_time && fmove < 0) {
        fmove = 0;
    }

    for (i = 0; i < 3; i++) {
        wishvel[i] = forward[i] * fmove + right[i] * smove;
    }

    if ((int)sv_player->v.movetype != MOVETYPE_WALK) {
        wishvel[2] = cmd.upmove;
    } else {
        wishvel[2] = 0;
    }

    VectorCopy(wishvel, wishdir);
    wishspeed = VectorNormalize(wishdir);
    if (wishspeed > sv_maxspeed.value) {
        VectorScale(wishvel, sv_maxspeed.value / wishspeed, wishvel);
        wishspeed = sv_maxspeed.value;
    }

    if (sv_player->v.movetype == MOVETYPE_NOCLIP) { // noclip
        VectorCopy(wishvel, velocity);
    } else if (onground) {
        SV_UserFriction();
        SV_Accelerate();
    } else { // not on ground, so little effect on velocity
        SV_AirAccelerate(wishvel);
    }
}

/*
===================
SV_ClientThink

the move fields specify an intended velocity in pix/sec
the angle fields specify an exact angular motion in degrees
===================
*/
void SV_ClientThink(void)
{
    vec3_t v_angle;

    if (sv_player->v.movetype == MOVETYPE_NONE) {
        return;
    }

    onground = (int)sv_player->v.flags & FL_ONGROUND;

    origin = sv_player->v.origin;
    velocity = sv_player->v.velocity;

    DropPunchAngle();

    //
    // if dead, behave differently
    //
    if (sv_player->v.health <= 0) {
        return;
    }

    //
    // angles
    // show 1/3 the pitch angle and all the roll angle
    cmd = host_client->cmd;
    angles = sv_player->v.angles;

    VectorAdd(sv_player->v.v_angle, sv_player->v.punchangle, v_angle);
    angles[ROLL] = V_CalcRoll(sv_player->v.angles, sv_player->v.velocity) * 4;
    if (!sv_player->v.fixangle) {
        angles[PITCH] = -v_angle[PITCH] / 3;
        angles[YAW] = v_angle[YAW];
    }

    if ((int)sv_player->v.flags & FL_WATERJUMP) {
        SV_WaterJump();

        return;
    }

    //
    // walk
    //
    if ((sv_player->v.waterlevel >= 2) && (sv_player->v.movetype != MOVETYPE_NOCLIP)) {
        SV_WaterMove();

        return;
    }

    SV_AirMove();
}

/*
===================
SV_ReadClientMove
===================
*/
void SV_ReadClientMove(usercmd_t* move)
{
    int i;
    vec3_t angle;
    int bits;

    // read ping time
    host_client->ping_times[host_client->num_pings % NUM_PING_TIMES] = sv.time - MSG_ReadFloat();
    host_client->num_pings++;

    // read current angles
    for (i = 0; i < 3; i++) {
        angle[i] = MSG_ReadAngle();
    }

    VectorCopy(angle, host_client->edict->v.v_angle);

    // read movement
    move->forwardmove = MSG_ReadShort();
    move->sidemove = MSG_ReadShort();
    move->upmove = MSG_ReadShort();

    // read buttons
    bits = MSG_ReadByte();
    host_client->edict->v.button0 = bits & 1;
    host_client->edict->v.button2 = (bits & 2) >> 1;

    i = MSG_ReadByte();
    if (i) {
        host_client->edict->v.impulse = i;
    }

}

/*
===================
SV_ReadClientMessage

Returns false if the client should be killed
===================
*/
qboolean SV_ReadClientMessage(void)
{
    int ret;
    int msg_cmd;
    char* s;

    do {
    nextmsg:
        ret = NET_GetMessage(host_client->netconnection);
        if (ret == -1) {
            Sys_Printf("SV_ReadClientMessage: NET_GetMessage failed\n");

            return false;
        }

        if (!ret) {
            return true;
        }

        MSG_BeginReading();

        while (1) {
            if (!host_client->active) {
                return false; // a command caused an error
            }

            if (msg_badread) {
                Sys_Printf("SV_ReadClientMessage: badread\n");

                return false;
            }

            msg_cmd = MSG_ReadChar();

            switch (msg_cmd) {
            case -1:
                goto nextmsg; // end of message

            default:
                Sys_Printf("SV_ReadClientMessage: unknown command char\n");

                return false;

            case clc_nop:
                //				Sys_Printf ("clc_nop\n");
                break;

            case clc_stringcmd:
                s = MSG_ReadString();
                if (host_client->privileged) {
                    ret = 2;
                } else {
                    ret = 0;
                }

                if (Q_strncasecmp(s, "status", 6) == 0) {
                    ret = 1;
                } else if (Q_strncasecmp(s, "god", 3) == 0) {
                    ret = 1;
                } else if (Q_strncasecmp(s, "notarget", 8) == 0) {
                    ret = 1;
                } else if (Q_strncasecmp(s, "fly", 3) == 0) {
                    ret = 1;
                } else if (Q_strncasecmp(s, "name", 4) == 0) {
                    ret = 1;
                } else if (Q_strncasecmp(s, "noclip", 6) == 0) {
                    ret = 1;
                } else if (Q_strncasecmp(s, "say", 3) == 0) {
                    ret = 1;
                } else if (Q_strncasecmp(s, "say_team", 8) == 0) {
                    ret = 1;
                } else if (Q_strncasecmp(s, "tell", 4) == 0) {
                    ret = 1;
                } else if (Q_strncasecmp(s, "color", 5) == 0) {
                    ret = 1;
                } else if (Q_strncasecmp(s, "kill", 4) == 0) {
                    ret = 1;
                } else if (Q_strncasecmp(s, "pause", 5) == 0) {
                    ret = 1;
                } else if (Q_strncasecmp(s, "spawn", 5) == 0) {
                    ret = 1;
                } else if (Q_strncasecmp(s, "begin", 5) == 0) {
                    ret = 1;
                } else if (Q_strncasecmp(s, "prespawn", 8) == 0) {
                    ret = 1;
                } else if (Q_strncasecmp(s, "kick", 4) == 0) {
                    ret = 1;
                } else if (Q_strncasecmp(s, "ping", 4) == 0) {
                    ret = 1;
                } else if (Q_strncasecmp(s, "give", 4) == 0) {
                    ret = 1;
                } else if (Q_strncasecmp(s, "ban", 3) == 0) {
                    ret = 1;
                }

                if (ret == 2) {
                    Cbuf_InsertText(s);
                } else if (ret == 1) {
                    Cmd_ExecuteString(s, src_client);
                } else {
                    Con_DPrintf("%s tried to %s\n", host_client->name, s);
                }

                break;

            case clc_disconnect:
                //				Sys_Printf ("SV_ReadClientMessage: client disconnected\n");
                return false;

            case clc_move:
                SV_ReadClientMove(&host_client->cmd);
                break;
            }
        }
    } while (ret == 1);

    return true;
}

/*
==================
SV_RunClients
==================
*/
void SV_RunClients(void)
{
    int i;

    for (i = 0, host_client = svs.clients; i < svs.maxclients;
        i++, host_client++) {
        if (!host_client->active) {
            continue;
        }

        sv_player = host_client->edict;

        if (!SV_ReadClientMessage()) {
            SV_DropClient(false); // client misbehaved...
            continue;
        }

        if (!host_client->spawned) {
            // clear client movement until a new packet is received
            memset(&host_client->cmd, 0, sizeof(host_client->cmd));
            continue;
        }

        // always pause in single player if in console or menus
        if (!sv.paused && (svs.maxclients > 1 || key_dest == key_game)) {
            SV_ClientThink();
        }
    }
}
