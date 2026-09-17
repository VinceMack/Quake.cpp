// sw_sprite.cpp -- Sprite Model Transformation, Frustum Clipping and Rasterization Setup
#include "render/software/sw_sprite.hpp"
#include "client/client_types.hpp"
#include "ui/console.hpp"

#include <cmath>
#include <cstring>

using namespace Math;
using namespace Common;
using namespace Console;
using namespace Client;

namespace Render {

void D_DrawSprite();

spritedesc_t r_spritedesc{};

static int sprite_width = 0, sprite_height = 0;
static int clip_current = 0;
static vec5_t clip_verts[2][MAXWORKINGVERTS]{};

void R_RotateSprite(float beam_len)
{
    if (beam_len == 0.0f) {
        return;
    }
    Vector3 vec = r_spritedesc.vpn * -beam_len;
    r_entorigin += vec;
    modelorg -= vec;
}

int R_ClipSpriteFace(int nump, clipplane_t* pclipplane)
{
    float dists[MAXWORKINGVERTS + 1];
    float clipdist = pclipplane->dist;
    const float* pclipnormal = &pclipplane->normal.x;

    float *in, *outstep;
    if (clip_current) {
        in = clip_verts[1][0];
        outstep = clip_verts[0][0];
        clip_current = 0;
    } else {
        in = clip_verts[0][0];
        outstep = clip_verts[1][0];
        clip_current = 1;
    }

    float* instep = in;
    for (int i = 0; i < nump; i++, instep += sizeof(vec5_t) / sizeof(float)) {
        dists[i] = DotProduct(instep, pclipnormal) - clipdist;
    }
    dists[nump] = dists[0];
    std::memcpy(instep, in, sizeof(vec5_t));

    instep = in;
    int outcount = 0;
    for (int i = 0; i < nump; i++, instep += sizeof(vec5_t) / sizeof(float)) {
        if (dists[i] >= 0) {
            std::memcpy(outstep, instep, sizeof(vec5_t));
            outstep += sizeof(vec5_t) / sizeof(float);
            outcount++;
        }
        if (dists[i] == 0 || dists[i + 1] == 0) {
            continue;
        }
        if ((dists[i] > 0) == (dists[i + 1] > 0)) {
            continue;
        }
        float frac = dists[i] / (dists[i] - dists[i + 1]);
        float* vert2 = instep + sizeof(vec5_t) / sizeof(float);
        outstep[0] = instep[0] + frac * (vert2[0] - instep[0]);
        outstep[1] = instep[1] + frac * (vert2[1] - instep[1]);
        outstep[2] = instep[2] + frac * (vert2[2] - instep[2]);
        outstep[3] = instep[3] + frac * (vert2[3] - instep[3]);
        outstep[4] = instep[4] + frac * (vert2[4] - instep[4]);
        outstep += sizeof(vec5_t) / sizeof(float);
        outcount++;
    }
    return outcount;
}

void R_SetupAndDrawSprite()
{
    float dot = r_spritedesc.vpn.dot(modelorg);
    if (dot >= 0) {
        return;
    }

    Vector3 right = r_spritedesc.vright * r_spritedesc.pspriteframe->right;
    Vector3 up = r_spritedesc.vup * r_spritedesc.pspriteframe->up;
    Vector3 left = r_spritedesc.vright * r_spritedesc.pspriteframe->left;
    Vector3 down = r_spritedesc.vup * r_spritedesc.pspriteframe->down;

    vec5_t* pverts = clip_verts[0];
    pverts[0][0] = r_entorigin.x + up.x + left.x;
    pverts[0][1] = r_entorigin.y + up.y + left.y;
    pverts[0][2] = r_entorigin.z + up.z + left.z;
    pverts[0][3] = 0;
    pverts[0][4] = 0;

    pverts[1][0] = r_entorigin.x + up.x + right.x;
    pverts[1][1] = r_entorigin.y + up.y + right.y;
    pverts[1][2] = r_entorigin.z + up.z + right.z;
    pverts[1][3] = static_cast<float>(sprite_width);
    pverts[1][4] = 0;

    pverts[2][0] = r_entorigin.x + down.x + right.x;
    pverts[2][1] = r_entorigin.y + down.y + right.y;
    pverts[2][2] = r_entorigin.z + down.z + right.z;
    pverts[2][3] = static_cast<float>(sprite_width);
    pverts[2][4] = static_cast<float>(sprite_height);

    pverts[3][0] = r_entorigin.x + down.x + left.x;
    pverts[3][1] = r_entorigin.y + down.y + left.y;
    pverts[3][2] = r_entorigin.z + down.z + left.z;
    pverts[3][3] = 0;
    pverts[3][4] = static_cast<float>(sprite_height);

    int nump = 4;
    clip_current = 0;
    for (int i = 0; i < 4; i++) {
        nump = R_ClipSpriteFace(nump, &view_clipplanes[i]);
        if (nump < 3) {
            return;
        }
        if (nump >= MAXWORKINGVERTS) {
            Sys_Error("R_SetupAndDrawSprite: too many points");
        }
    }

    emitpoint_t outverts[MAXWORKINGVERTS + 1];
    float* pv = &clip_verts[clip_current][0][0];
    r_spritedesc.nearzi = -999999.0f;
    for (int i = 0; i < nump; i++) {
        Vector3 local = Vector3(pv[0], pv[1], pv[2]) - r_origin;
        Vector3 transformed;
        TransformVector(local, transformed);
        if (transformed.z < NEAR_CLIP) {
            transformed.z = (vec_t)NEAR_CLIP;
        }
        emitpoint_t* pout = &outverts[i];
        pout->zi = static_cast<float>(1.0 / transformed.z);
        if (pout->zi > r_spritedesc.nearzi) {
            r_spritedesc.nearzi = pout->zi;
        }
        pout->s = pv[3];
        pout->t = pv[4];
        float scale = xscale * pout->zi;
        pout->u = (xcenter + scale * transformed.x);
        scale = yscale * pout->zi;
        pout->v = (ycenter - scale * transformed.y);
        pv += sizeof(vec5_t) / sizeof(*pv);
    }

    r_spritedesc.nump = nump;
    r_spritedesc.pverts = outverts;
    D_DrawSprite();
}

mspriteframe_t* R_GetSpriteframe(msprite_t* psprite)
{
    int frame = currententity->frame;
    if ((frame >= psprite->numframes) || (frame < 0)) {
        Con_Printf("R_DrawSprite: no such frame %d\n", frame);
        frame = 0;
    }
    if (psprite->frames[frame].type == spriteframetype_t::SPR_SINGLE) {
        return psprite->frames[frame].frameptr;
    }

    mspritegroup_t* pspritegroup = (mspritegroup_t*)psprite->frames[frame].frameptr;
    float* pintervals = pspritegroup->intervals;
    int numframes = pspritegroup->numframes;
    float fullinterval = pintervals[numframes - 1];
    float time = static_cast<float>(cl.time + currententity->syncbase);
    float targettime = time - ((int)(time / fullinterval)) * fullinterval;
    int i = 0;
    for (; i < (numframes - 1); i++) {
        if (pintervals[i] > targettime) {
            break;
        }
    }
    return pspritegroup->frames[i];
}

void R_DrawSprite()
{
    msprite_t* psprite = static_cast<msprite_t*>(Model::Mod_Extradata(currententity->model));
    r_spritedesc.pspriteframe = R_GetSpriteframe(psprite);
    sprite_width = r_spritedesc.pspriteframe->width;
    sprite_height = r_spritedesc.pspriteframe->height;

    if (psprite->type == SPR_FACING_UPRIGHT) {
        Vector3 tvec = -modelorg;
        tvec.normalize();
        float dot = tvec.z;
        if ((dot > 0.999848f) || (dot < -0.999848f)) {
            return;
        }
        r_spritedesc.vup = Vector3(0, 0, 1);
        r_spritedesc.vright = Vector3(tvec.y, -tvec.x, 0);
        r_spritedesc.vright.normalize();
        r_spritedesc.vpn = Vector3(-r_spritedesc.vright.y, r_spritedesc.vright.x, 0);
    } else if (psprite->type == SPR_VP_PARALLEL) {
        r_spritedesc.vup = vup;
        r_spritedesc.vright = vright;
        r_spritedesc.vpn = vpn;
    } else if (psprite->type == SPR_VP_PARALLEL_UPRIGHT) {
        float dot = vpn.z;
        if ((dot > 0.999848f) || (dot < -0.999848f)) {
            return;
        }
        r_spritedesc.vup = Vector3(0, 0, 1);
        r_spritedesc.vright = Vector3(vpn.y, -vpn.x, 0);
        r_spritedesc.vright.normalize();
        r_spritedesc.vpn = Vector3(-r_spritedesc.vright.y, r_spritedesc.vright.x, 0);
    } else if (psprite->type == SPR_ORIENTED) {
        AngleVectors(currententity->angles, r_spritedesc.vpn, r_spritedesc.vright,
            r_spritedesc.vup);
    } else if (psprite->type == SPR_VP_PARALLEL_ORIENTED) {
        float angle = static_cast<float>(currententity->angles[ROLL] * (M_PI * 2 / 360));
        float sr = std::sin(angle);
        float cr = std::cos(angle);
        r_spritedesc.vpn = vpn;
        r_spritedesc.vright = vright * cr + vup * sr;
        r_spritedesc.vup = vright * -sr + vup * cr;
    } else {
        Sys_Error("R_DrawSprite: Bad sprite type %d", psprite->type);
    }
    R_RotateSprite(psprite->beamlength);
    R_SetupAndDrawSprite();
}

} // namespace Render
