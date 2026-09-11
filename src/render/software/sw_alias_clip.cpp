// sw_alias_clip.cpp -- Alias Model (MDL) Polygon Frustum and Z-Clipping
#include "render/software/sw_alias_clip.hpp"
#include "world/bsp_format.hpp"

namespace Render {

void D_PolysetDraw();
void R_AliasProjectFinalVert(finalvert_t* fv, auxvert_t* av);

static finalvert_t aclip_fv[2][8];
static auxvert_t aclip_av[8];

static void R_Alias_clip_z(finalvert_t* pfv0, finalvert_t* pfv1, finalvert_t* out)
{
    float scale;
    auxvert_t avout;
    auxvert_t* pav0 = &aclip_av[pfv0 - &aclip_fv[0][0]];
    auxvert_t* pav1 = &aclip_av[pfv1 - &aclip_fv[0][0]];

    if (pfv0->v[1] >= pfv1->v[1]) {
        scale = (ALIAS_Z_CLIP_PLANE - pav0->fv[2]) / (pav1->fv[2] - pav0->fv[2]);
        avout.fv[0] = pav0->fv[0] + (pav1->fv[0] - pav0->fv[0]) * scale;
        avout.fv[1] = pav0->fv[1] + (pav1->fv[1] - pav0->fv[1]) * scale;
        avout.fv[2] = ALIAS_Z_CLIP_PLANE;
        out->v[2] = static_cast<int>(pfv0->v[2] + (pfv1->v[2] - pfv0->v[2]) * scale);
        out->v[3] = static_cast<int>(pfv0->v[3] + (pfv1->v[3] - pfv0->v[3]) * scale);
        out->v[4] = static_cast<int>(pfv0->v[4] + (pfv1->v[4] - pfv0->v[4]) * scale);
    } else {
        scale = (ALIAS_Z_CLIP_PLANE - pav1->fv[2]) / (pav0->fv[2] - pav1->fv[2]);
        avout.fv[0] = pav1->fv[0] + (pav0->fv[0] - pav1->fv[0]) * scale;
        avout.fv[1] = pav1->fv[1] + (pav0->fv[1] - pav1->fv[1]) * scale;
        avout.fv[2] = ALIAS_Z_CLIP_PLANE;
        out->v[2] = static_cast<int>(pfv1->v[2] + (pfv0->v[2] - pfv1->v[2]) * scale);
        out->v[3] = static_cast<int>(pfv1->v[3] + (pfv0->v[3] - pfv1->v[3]) * scale);
        out->v[4] = static_cast<int>(pfv1->v[4] + (pfv0->v[4] - pfv1->v[4]) * scale);
    }
    R_AliasProjectFinalVert(out, &avout);
    if (out->v[0] < r_refdef.aliasvrect.x) {
        out->flags |= ALIAS_LEFT_CLIP;
    }
    if (out->v[1] < r_refdef.aliasvrect.y) {
        out->flags |= ALIAS_TOP_CLIP;
    }
    if (out->v[0] > r_refdef.aliasvrectright) {
        out->flags |= ALIAS_RIGHT_CLIP;
    }
    if (out->v[1] > r_refdef.aliasvrectbottom) {
        out->flags |= ALIAS_BOTTOM_CLIP;
    }
}

static void R_Alias_clip_left(finalvert_t* pfv0, finalvert_t* pfv1, finalvert_t* out)
{
    float scale;
    if (pfv0->v[1] >= pfv1->v[1]) {
        scale = static_cast<float>(r_refdef.aliasvrect.x - pfv0->v[0]) / (pfv1->v[0] - pfv0->v[0]);
        for (int i = 0; i < 6; i++) {
            out->v[i] = static_cast<int>(pfv0->v[i] + (pfv1->v[i] - pfv0->v[i]) * scale + 0.5f);
        }
    } else {
        scale = static_cast<float>(r_refdef.aliasvrect.x - pfv1->v[0]) / (pfv0->v[0] - pfv1->v[0]);
        for (int i = 0; i < 6; i++) {
            out->v[i] = static_cast<int>(pfv1->v[i] + (pfv0->v[i] - pfv1->v[i]) * scale + 0.5f);
        }
    }
}

static void R_Alias_clip_right(finalvert_t* pfv0, finalvert_t* pfv1, finalvert_t* out)
{
    float scale;
    if (pfv0->v[1] >= pfv1->v[1]) {
        scale = static_cast<float>(r_refdef.aliasvrectright - pfv0->v[0]) / (pfv1->v[0] - pfv0->v[0]);
        for (int i = 0; i < 6; i++) {
            out->v[i] = static_cast<int>(pfv0->v[i] + (pfv1->v[i] - pfv0->v[i]) * scale + 0.5f);
        }
    } else {
        scale = static_cast<float>(r_refdef.aliasvrectright - pfv1->v[0]) / (pfv0->v[0] - pfv1->v[0]);
        for (int i = 0; i < 6; i++) {
            out->v[i] = static_cast<int>(pfv1->v[i] + (pfv0->v[i] - pfv1->v[i]) * scale + 0.5f);
        }
    }
}

static void R_Alias_clip_top(finalvert_t* pfv0, finalvert_t* pfv1, finalvert_t* out)
{
    float scale;
    if (pfv0->v[1] >= pfv1->v[1]) {
        scale = static_cast<float>(r_refdef.aliasvrect.y - pfv0->v[1]) / (pfv1->v[1] - pfv0->v[1]);
        for (int i = 0; i < 6; i++) {
            out->v[i] = static_cast<int>(pfv0->v[i] + (pfv1->v[i] - pfv0->v[i]) * scale + 0.5f);
        }
    } else {
        scale = static_cast<float>(r_refdef.aliasvrect.y - pfv1->v[1]) / (pfv0->v[1] - pfv1->v[1]);
        for (int i = 0; i < 6; i++) {
            out->v[i] = static_cast<int>(pfv1->v[i] + (pfv0->v[i] - pfv1->v[i]) * scale + 0.5f);
        }
    }
}

static void R_Alias_clip_bottom(finalvert_t* pfv0, finalvert_t* pfv1, finalvert_t* out)
{
    float scale;
    if (pfv0->v[1] >= pfv1->v[1]) {
        scale = static_cast<float>(r_refdef.aliasvrectbottom - pfv0->v[1]) / (pfv1->v[1] - pfv0->v[1]);
        for (int i = 0; i < 6; i++) {
            out->v[i] = static_cast<int>(pfv0->v[i] + (pfv1->v[i] - pfv0->v[i]) * scale + 0.5f);
        }
    } else {
        scale = static_cast<float>(r_refdef.aliasvrectbottom - pfv1->v[1]) / (pfv0->v[1] - pfv1->v[1]);
        for (int i = 0; i < 6; i++) {
            out->v[i] = static_cast<int>(pfv1->v[i] + (pfv0->v[i] - pfv1->v[i]) * scale + 0.5f);
        }
    }
}

static int R_AliasClip(finalvert_t* in, finalvert_t* out, int flag, int count,
    void (*clip)(finalvert_t* pfv0, finalvert_t* pfv1, finalvert_t* out))
{
    int j = count - 1;
    int k = 0;
    for (int i = 0; i < count; j = i, i++) {
        int oldflags = in[j].flags & flag;
        int flags = in[i].flags & flag;
        if (flags && oldflags) {
            continue;
        }
        if (oldflags ^ flags) {
            clip(&in[j], &in[i], &out[k]);
            out[k].flags = 0;
            if (out[k].v[0] < r_refdef.aliasvrect.x) {
                out[k].flags |= ALIAS_LEFT_CLIP;
            }
            if (out[k].v[1] < r_refdef.aliasvrect.y) {
                out[k].flags |= ALIAS_TOP_CLIP;
            }
            if (out[k].v[0] > r_refdef.aliasvrectright) {
                out[k].flags |= ALIAS_RIGHT_CLIP;
            }
            if (out[k].v[1] > r_refdef.aliasvrectbottom) {
                out[k].flags |= ALIAS_BOTTOM_CLIP;
            }
            k++;
        }
        if (!flags) {
            out[k] = in[i];
            k++;
        }
    }
    return k;
}

void R_AliasClipTriangle(mtriangle_t* ptri)
{
    int k, pingpong;
    mtriangle_t mtri;

    if (ptri->facesfront) {
        aclip_fv[0][0] = pfinalverts[ptri->vertindex[0]];
        aclip_fv[0][1] = pfinalverts[ptri->vertindex[1]];
        aclip_fv[0][2] = pfinalverts[ptri->vertindex[2]];
    } else {
        for (int i = 0; i < 3; i++) {
            aclip_fv[0][i] = pfinalverts[ptri->vertindex[i]];
            if (!ptri->facesfront && (aclip_fv[0][i].flags & ALIAS_ONSEAM)) {
                aclip_fv[0][i].v[2] += r_affinetridesc.seamfixupX16;
            }
        }
    }

    unsigned clipflags = aclip_fv[0][0].flags | aclip_fv[0][1].flags | aclip_fv[0][2].flags;
    if (clipflags & ALIAS_Z_CLIP) {
        for (int i = 0; i < 3; i++) {
            aclip_av[i] = pauxverts[ptri->vertindex[i]];
        }
        k = R_AliasClip(aclip_fv[0], aclip_fv[1], ALIAS_Z_CLIP, 3, R_Alias_clip_z);
        if (k == 0) {
            return;
        }
        pingpong = 1;
        clipflags = aclip_fv[1][0].flags | aclip_fv[1][1].flags | aclip_fv[1][2].flags;
    } else {
        pingpong = 0;
        k = 3;
    }

    if (clipflags & ALIAS_LEFT_CLIP) {
        k = R_AliasClip(aclip_fv[pingpong], aclip_fv[pingpong ^ 1], ALIAS_LEFT_CLIP, k, R_Alias_clip_left);
        if (k == 0) {
            return;
        }
        pingpong ^= 1;
    }
    if (clipflags & ALIAS_RIGHT_CLIP) {
        k = R_AliasClip(aclip_fv[pingpong], aclip_fv[pingpong ^ 1], ALIAS_RIGHT_CLIP, k, R_Alias_clip_right);
        if (k == 0) {
            return;
        }
        pingpong ^= 1;
    }
    if (clipflags & ALIAS_BOTTOM_CLIP) {
        k = R_AliasClip(aclip_fv[pingpong], aclip_fv[pingpong ^ 1], ALIAS_BOTTOM_CLIP, k, R_Alias_clip_bottom);
        if (k == 0) {
            return;
        }
        pingpong ^= 1;
    }
    if (clipflags & ALIAS_TOP_CLIP) {
        k = R_AliasClip(aclip_fv[pingpong], aclip_fv[pingpong ^ 1], ALIAS_TOP_CLIP, k, R_Alias_clip_top);
        if (k == 0) {
            return;
        }
        pingpong ^= 1;
    }

    for (int i = 0; i < k; i++) {
        if (aclip_fv[pingpong][i].v[0] < r_refdef.aliasvrect.x) {
            aclip_fv[pingpong][i].v[0] = r_refdef.aliasvrect.x;
        } else if (aclip_fv[pingpong][i].v[0] > r_refdef.aliasvrectright) {
            aclip_fv[pingpong][i].v[0] = r_refdef.aliasvrectright;
        }
        if (aclip_fv[pingpong][i].v[1] < r_refdef.aliasvrect.y) {
            aclip_fv[pingpong][i].v[1] = r_refdef.aliasvrect.y;
        } else if (aclip_fv[pingpong][i].v[1] > r_refdef.aliasvrectbottom) {
            aclip_fv[pingpong][i].v[1] = r_refdef.aliasvrectbottom;
        }
        aclip_fv[pingpong][i].flags = 0;
    }

    mtri.facesfront = ptri->facesfront;
    r_affinetridesc.ptriangles = &mtri;
    r_affinetridesc.pfinalverts = aclip_fv[pingpong];
    mtri.vertindex[0] = 0;
    for (int i = 1; i < k - 1; i++) {
        mtri.vertindex[1] = i;
        mtri.vertindex[2] = i + 1;
        D_PolysetDraw();
    }
}

} // namespace Render
