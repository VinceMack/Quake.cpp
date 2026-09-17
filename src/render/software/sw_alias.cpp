// sw_alias.cpp -- Alias Model (MDL) Transformation, Lighting, and Skin Setup
#include "render/software/sw_alias.hpp"
#include "render/software/sw_alias_clip.hpp"
#include "client/client_types.hpp"
#include "ui/console.hpp"

#include <cmath>

using namespace Math;
using namespace Common;
using namespace Console;
using namespace Client;
using namespace Model;

namespace Render {

inline constexpr int LIGHT_MIN = 5;

affinetridesc_t r_affinetridesc{};
void* acolormap = nullptr;
static trivertx_t* r_apverts = nullptr;

Vector3 r_plightvec{};
static int r_ambientlight = 0;
static float r_shadelight = 0.0f;
static float ziscale = 0.0f;
static model_t* pmodel = nullptr;

static Vector3 alias_forward{}, alias_right{}, alias_up{};
static maliasskindesc_t* pskindesc = nullptr;
static int r_anumverts = 0;

static float aliastransform[3][4]{};

mdl_t* pmdl = nullptr;
aliashdr_t* paliashdr = nullptr;
finalvert_t* pfinalverts = nullptr;
auxvert_t* pauxverts = nullptr;
int r_amodels_drawn = 0;
int a_skinwidth = 0;

struct aedge_t {
    int index0;
    int index1;
};

static constexpr aedge_t aedges[12] = {
    { 0, 1 }, { 1, 2 }, { 2, 3 }, { 3, 0 }, { 4, 5 }, { 5, 6 },
    { 6, 7 }, { 7, 4 }, { 0, 5 }, { 1, 4 }, { 2, 7 }, { 3, 6 }
};

void R_InitVertexNormals()
{
    static bool initialized = false;
    if (initialized) return;
    initialized = true;

    const float X = 0.525731112119133606f;
    const float Z = 0.850650808352039932f;
    const float verts[12][3] = {
        {-X, 0.0f, Z}, {X, 0.0f, Z}, {-X, 0.0f, -Z}, {X, 0.0f, -Z},
        {0.0f, Z, X}, {0.0f, Z, -X}, {0.0f, -Z, X}, {0.0f, -Z, -X},
        {Z, X, 0.0f}, {-Z, X, 0.0f}, {Z, -X, 0.0f}, {-Z, -X, 0.0f},
    };
    const int faces[20][3] = {
        {0, 4, 1}, {0, 9, 4}, {9, 5, 4}, {4, 5, 8}, {4, 8, 1},
        {8, 10, 1}, {8, 3, 10}, {5, 3, 8}, {5, 2, 3}, {2, 7, 3},
        {7, 10, 3}, {7, 6, 10}, {7, 11, 6}, {11, 0, 6}, {0, 1, 6},
        {6, 1, 10}, {9, 0, 11}, {9, 11, 2}, {9, 2, 5}, {7, 2, 11}
    };
    const int subdiv = 4;
    float temp[400][3];
    int num_temp = 0;

    for (int f = 0; f < 20; f++) {
        const float* a = verts[faces[f][0]];
        const float* b = verts[faces[f][1]];
        const float* c = verts[faces[f][2]];
        for (int i = subdiv; i >= subdiv / 2; i--) {
            for (int j = subdiv - i; j >= 0; j--) {
                int k = subdiv - i - j;
                float px = (i * a[0] + j * b[0] + k * c[0]) / subdiv;
                float py = (i * a[1] + j * b[1] + k * c[1]) / subdiv;
                float pz = (i * a[2] + j * b[2] + k * c[2]) / subdiv;
                float len = std::sqrt(px * px + py * py + pz * pz);
                if (len > 0) {
                    temp[num_temp][0] = px / len;
                    temp[num_temp][1] = py / len;
                    temp[num_temp][2] = pz / len;
                    num_temp++;
                }
            }
        }
        for (int i = subdiv / 2 - 1; i >= 0; i--) {
            for (int j = subdiv - i; j >= subdiv / 2; j--) {
                int k = subdiv - i - j;
                float px = (i * a[0] + j * b[0] + k * c[0]) / subdiv;
                float py = (i * a[1] + j * b[1] + k * c[1]) / subdiv;
                float pz = (i * a[2] + j * b[2] + k * c[2]) / subdiv;
                float len = std::sqrt(px * px + py * py + pz * pz);
                if (len > 0) {
                    temp[num_temp][0] = px / len;
                    temp[num_temp][1] = py / len;
                    temp[num_temp][2] = pz / len;
                    num_temp++;
                }
            }
        }
        for (int i = subdiv / 2 - 1; i >= 0; i--) {
            for (int j = subdiv / 2 - 1; j >= 0; j--) {
                int k = subdiv - i - j;
                if (k >= subdiv / 2) {
                    float px = (i * a[0] + j * b[0] + k * c[0]) / subdiv;
                    float py = (i * a[1] + j * b[1] + k * c[1]) / subdiv;
                    float pz = (i * a[2] + j * b[2] + k * c[2]) / subdiv;
                    float len = std::sqrt(px * px + py * py + pz * pz);
                    if (len > 0) {
                        temp[num_temp][0] = px / len;
                        temp[num_temp][1] = py / len;
                        temp[num_temp][2] = pz / len;
                        num_temp++;
                    }
                }
            }
        }
    }

    int out_idx = 0;
    for (int i = 0; i < num_temp && out_idx < NUMVERTEXNORMALS; i++) {
        bool dup = false;
        int ix = (int)std::round(temp[i][0] * 10000.0f);
        int iy = (int)std::round(temp[i][1] * 10000.0f);
        int iz = (int)std::round(temp[i][2] * 10000.0f);
        for (int j = 0; j < out_idx; j++) {
            int jx = (int)std::round(r_avertexnormals[j][0] * 10000.0f);
            int jy = (int)std::round(r_avertexnormals[j][1] * 10000.0f);
            int jz = (int)std::round(r_avertexnormals[j][2] * 10000.0f);
            if (ix == jx && iy == jy && iz == jz) {
                dup = true;
                break;
            }
        }
        if (!dup) {
            r_avertexnormals[out_idx][0] = temp[i][0];
            r_avertexnormals[out_idx][1] = temp[i][1];
            r_avertexnormals[out_idx][2] = temp[i][2];
            out_idx++;
        }
    }
}

bool R_AliasCheckBBox()
{
    float basepts[8][3];
    finalvert_t viewpts[16];
    auxvert_t viewaux[16];

    currententity->trivial_accept = 0;
    pmodel = currententity->model;
    aliashdr_t* pahdr = (aliashdr_t*)Mod_Extradata(pmodel);
    pmdl = reinterpret_cast<mdl_t*>(reinterpret_cast<byte*>(pahdr) + pahdr->model);
    R_AliasSetUpTransform(0);

    int frame = currententity->frame;
    if ((frame >= pmdl->numframes) || (frame < 0)) {
        Con_DPrintf("No such frame %d %s\n", frame, pmodel->name);
        frame = 0;
    }
    maliasframedesc_t* pframedesc = &pahdr->frames[frame];

    basepts[0][0] = basepts[1][0] = basepts[2][0] = basepts[3][0] = (float)pframedesc->bboxmin.v[0];
    basepts[4][0] = basepts[5][0] = basepts[6][0] = basepts[7][0] = (float)pframedesc->bboxmax.v[0];
    basepts[0][1] = basepts[3][1] = basepts[5][1] = basepts[6][1] = (float)pframedesc->bboxmin.v[1];
    basepts[1][1] = basepts[2][1] = basepts[4][1] = basepts[7][1] = (float)pframedesc->bboxmax.v[1];
    basepts[0][2] = basepts[1][2] = basepts[4][2] = basepts[5][2] = (float)pframedesc->bboxmin.v[2];
    basepts[2][2] = basepts[3][2] = basepts[6][2] = basepts[7][2] = (float)pframedesc->bboxmax.v[2];

    bool zclipped = false;
    bool zfullyclipped = true;
    int minz = 9999;
    for (int i = 0; i < 8; i++) {
        R_AliasTransformVector(&basepts[i][0], &viewaux[i].fv[0]);
        if (viewaux[i].fv[2] < ALIAS_Z_CLIP_PLANE) {
            viewpts[i].flags = ALIAS_Z_CLIP;
            zclipped = true;
        } else {
            if (viewaux[i].fv[2] < minz) {
                minz = static_cast<int>(viewaux[i].fv[2]);
            }
            viewpts[i].flags = 0;
            zfullyclipped = false;
        }
    }
    if (zfullyclipped) {
        return false;
    }

    int numv = 8;
    if (zclipped) {
        for (int i = 0; i < 12; i++) {
            finalvert_t* pv0 = &viewpts[aedges[i].index0];
            finalvert_t* pv1 = &viewpts[aedges[i].index1];
            auxvert_t* pa0 = &viewaux[aedges[i].index0];
            auxvert_t* pa1 = &viewaux[aedges[i].index1];
            if (pv0->flags ^ pv1->flags) {
                float frac = (ALIAS_Z_CLIP_PLANE - pa0->fv[2]) / (pa1->fv[2] - pa0->fv[2]);
                viewaux[numv].fv[0] = pa0->fv[0] + (pa1->fv[0] - pa0->fv[0]) * frac;
                viewaux[numv].fv[1] = pa0->fv[1] + (pa1->fv[1] - pa0->fv[1]) * frac;
                viewaux[numv].fv[2] = ALIAS_Z_CLIP_PLANE;
                viewpts[numv].flags = 0;
                numv++;
            }
        }
    }

    unsigned anyclip = 0;
    unsigned allclip = ALIAS_XY_CLIP_MASK;
    for (int i = 0; i < numv; i++) {
        if (viewpts[i].flags & ALIAS_Z_CLIP) {
            continue;
        }
        float zi = static_cast<float>(1.0 / viewaux[i].fv[2]);
        float v0 = (viewaux[i].fv[0] * xscale * zi) + xcenter;
        float v1 = (viewaux[i].fv[1] * yscale * zi) + ycenter;
        int flags = 0;
        if (v0 < r_refdef.fvrectx) {
            flags |= ALIAS_LEFT_CLIP;
        }
        if (v1 < r_refdef.fvrecty) {
            flags |= ALIAS_TOP_CLIP;
        }
        if (v0 > r_refdef.fvrectright) {
            flags |= ALIAS_RIGHT_CLIP;
        }
        if (v1 > r_refdef.fvrectbottom) {
            flags |= ALIAS_BOTTOM_CLIP;
        }
        anyclip |= flags;
        allclip &= flags;
    }
    if (allclip) {
        return false;
    }
    currententity->trivial_accept = !anyclip & !zclipped;
    if (currententity->trivial_accept) {
        if (minz > (r_aliastransition + (pmdl->size * r_resfudge))) {
            currententity->trivial_accept |= 2;
        }
    }
    return true;
}

void R_AliasTransformVector(const float* in, float* out)
{
    out[0] = DotProduct(in, aliastransform[0]) + aliastransform[0][3];
    out[1] = DotProduct(in, aliastransform[1]) + aliastransform[1][3];
    out[2] = DotProduct(in, aliastransform[2]) + aliastransform[2][3];
}

void R_AliasPreparePoints()
{
    stvert_t* pstverts = reinterpret_cast<stvert_t*>(reinterpret_cast<byte*>(paliashdr) + paliashdr->stverts);
    r_anumverts = pmdl->numverts;
    finalvert_t* fv = pfinalverts;
    auxvert_t* av = pauxverts;

    for (int i = 0; i < r_anumverts; i++, fv++, av++, r_apverts++, pstverts++) {
        R_AliasTransformFinalVert(fv, av, r_apverts, pstverts);
        if (av->fv[2] < ALIAS_Z_CLIP_PLANE) {
            fv->flags |= ALIAS_Z_CLIP;
        } else {
            R_AliasProjectFinalVert(fv, av);
            if (fv->v[0] < r_refdef.aliasvrect.x) {
                fv->flags |= ALIAS_LEFT_CLIP;
            }
            if (fv->v[1] < r_refdef.aliasvrect.y) {
                fv->flags |= ALIAS_TOP_CLIP;
            }
            if (fv->v[0] > r_refdef.aliasvrectright) {
                fv->flags |= ALIAS_RIGHT_CLIP;
            }
            if (fv->v[1] > r_refdef.aliasvrectbottom) {
                fv->flags |= ALIAS_BOTTOM_CLIP;
            }
        }
    }

    r_affinetridesc.numtriangles = 1;
    mtriangle_t* ptri = reinterpret_cast<mtriangle_t*>(reinterpret_cast<byte*>(paliashdr) + paliashdr->triangles);
    finalvert_t* paclip_fv[3];
    for (int i = 0; i < pmdl->numtris; i++, ptri++) {
        paclip_fv[0] = &pfinalverts[ptri->vertindex[0]];
        paclip_fv[1] = &pfinalverts[ptri->vertindex[1]];
        paclip_fv[2] = &pfinalverts[ptri->vertindex[2]];
        if (paclip_fv[0]->flags & paclip_fv[1]->flags & paclip_fv[2]->flags & (ALIAS_XY_CLIP_MASK | ALIAS_Z_CLIP)) {
            continue;
        }
        if (!((paclip_fv[0]->flags | paclip_fv[1]->flags | paclip_fv[2]->flags) & (ALIAS_XY_CLIP_MASK | ALIAS_Z_CLIP))) {
            r_affinetridesc.pfinalverts = pfinalverts;
            r_affinetridesc.ptriangles = ptri;
            D_PolysetDraw();
        } else {
            R_AliasClipTriangle(ptri);
        }
    }
}

void R_AliasSetUpTransform(int trivial_accept)
{
    float rotationmatrix[3][4], t2matrix[3][4];
    static float tmatrix[3][4];
    static float viewmatrix[3][4];
    Vector3 angles;

    angles[ROLL] = currententity->angles[ROLL];
    angles[PITCH] = -currententity->angles[PITCH];
    angles[YAW] = currententity->angles[YAW];
    AngleVectors(angles, alias_forward, alias_right, alias_up);

    tmatrix[0][0] = pmdl->scale[0];
    tmatrix[1][1] = pmdl->scale[1];
    tmatrix[2][2] = pmdl->scale[2];
    tmatrix[0][3] = pmdl->scale_origin[0];
    tmatrix[1][3] = pmdl->scale_origin[1];
    tmatrix[2][3] = pmdl->scale_origin[2];

    for (int i = 0; i < 3; i++) {
        t2matrix[i][0] = alias_forward[i];
        t2matrix[i][1] = -alias_right[i];
        t2matrix[i][2] = alias_up[i];
    }
    t2matrix[0][3] = -modelorg[0];
    t2matrix[1][3] = -modelorg[1];
    t2matrix[2][3] = -modelorg[2];

    R_ConcatTransforms(t2matrix, tmatrix, rotationmatrix);

    VectorCopy(vright, viewmatrix[0]);
    VectorCopy(vup, viewmatrix[1]);
    VectorInverse(viewmatrix[1]);
    VectorCopy(vpn, viewmatrix[2]);

    R_ConcatTransforms(viewmatrix, rotationmatrix, aliastransform);

    if (trivial_accept) {
        for (int i = 0; i < 4; i++) {
            aliastransform[0][i] *= aliasxscale * static_cast<float>(1.0 / ((float)0x8000 * 0x10000));
            aliastransform[1][i] *= aliasyscale * static_cast<float>(1.0 / ((float)0x8000 * 0x10000));
            aliastransform[2][i] *= static_cast<float>(1.0 / ((float)0x8000 * 0x10000));
        }
    }
}

void R_AliasTransformFinalVert(finalvert_t* fv, auxvert_t* av, trivertx_t* pverts, stvert_t* pstverts)
{
    av->fv[0] = DotProduct(pverts->v, aliastransform[0]) + aliastransform[0][3];
    av->fv[1] = DotProduct(pverts->v, aliastransform[1]) + aliastransform[1][3];
    av->fv[2] = DotProduct(pverts->v, aliastransform[2]) + aliastransform[2][3];
    fv->v[2] = pstverts->s;
    fv->v[3] = pstverts->t;
    fv->flags = pstverts->onseam;

    float* plightnormal = r_avertexnormals[pverts->lightnormalindex].data();
    float lightcos = DotProduct(plightnormal, r_plightvec);
    int temp = r_ambientlight;
    if (lightcos < 0) {
        temp += (int)(r_shadelight * lightcos);
        if (temp < 0) {
            temp = 0;
        }
    }
    fv->v[4] = temp;
}

void R_AliasTransformAndProjectFinalVerts(finalvert_t* fv, stvert_t* pstverts)
{
    trivertx_t* pverts = r_apverts;
    for (int i = 0; i < r_anumverts; i++, fv++, pverts++, pstverts++) {
        float zi = static_cast<float>(1.0 / (DotProduct(pverts->v, aliastransform[2]) + aliastransform[2][3]));
        fv->v[5] = static_cast<int>(zi);
        fv->v[0] = static_cast<int>(((DotProduct(pverts->v, aliastransform[0]) + aliastransform[0][3]) * zi) + aliasxcenter);
        fv->v[1] = static_cast<int>(((DotProduct(pverts->v, aliastransform[1]) + aliastransform[1][3]) * zi) + aliasycenter);
        fv->v[2] = pstverts->s;
        fv->v[3] = pstverts->t;
        fv->flags = pstverts->onseam;

        float* plightnormal = r_avertexnormals[pverts->lightnormalindex].data();
        float lightcos = DotProduct(plightnormal, r_plightvec);
        int temp = r_ambientlight;
        if (lightcos < 0) {
            temp += (int)(r_shadelight * lightcos);
            if (temp < 0) {
                temp = 0;
            }
        }
        fv->v[4] = temp;
    }
}

void R_AliasProjectFinalVert(finalvert_t* fv, auxvert_t* av)
{
    float zi = static_cast<float>(1.0 / av->fv[2]);
    fv->v[5] = static_cast<int>(zi * ziscale);
    fv->v[0] = static_cast<int>((av->fv[0] * aliasxscale * zi) + aliasxcenter);
    fv->v[1] = static_cast<int>((av->fv[1] * aliasyscale * zi) + aliasycenter);
}

void R_AliasPrepareUnclippedPoints()
{
    stvert_t* pstverts = reinterpret_cast<stvert_t*>(reinterpret_cast<byte*>(paliashdr) + paliashdr->stverts);
    r_anumverts = pmdl->numverts;
    finalvert_t* fv = pfinalverts;
    R_AliasTransformAndProjectFinalVerts(fv, pstverts);
    if (r_affinetridesc.drawtype) {
        D_PolysetDrawFinalVerts(fv, r_anumverts);
    }
    r_affinetridesc.pfinalverts = pfinalverts;
    r_affinetridesc.ptriangles = reinterpret_cast<mtriangle_t*>(reinterpret_cast<byte*>(paliashdr) + paliashdr->triangles);
    r_affinetridesc.numtriangles = pmdl->numtris;
    D_PolysetDraw();
}

void R_AliasSetupSkin()
{
    int skinnum = currententity->skinnum;
    if ((skinnum >= pmdl->numskins) || (skinnum < 0)) {
        Con_DPrintf("R_AliasSetupSkin: no such skin # %d\n", skinnum);
        skinnum = 0;
    }
    pskindesc = reinterpret_cast<maliasskindesc_t*>(reinterpret_cast<byte*>(paliashdr) + paliashdr->skindesc) + skinnum;
    a_skinwidth = pmdl->skinwidth;
    if (pskindesc->type == aliasskintype_t::ALIAS_SKIN_GROUP) {
        maliasskingroup_t* paliasskingroup = reinterpret_cast<maliasskingroup_t*>(reinterpret_cast<byte*>(paliashdr) + pskindesc->skin);
        float* pskinintervals = reinterpret_cast<float*>(reinterpret_cast<byte*>(paliashdr) + paliasskingroup->intervals);
        int numskins = paliasskingroup->numskins;
        float fullskininterval = pskinintervals[numskins - 1];
        float skintime = static_cast<float>(cl.time + currententity->syncbase);
        float skintargettime = skintime - ((int)(skintime / fullskininterval)) * fullskininterval;
        int i = 0;
        for (; i < (numskins - 1); i++) {
            if (pskinintervals[i] > skintargettime) {
                break;
            }
        }
        pskindesc = &paliasskingroup->skindescs[i];
    }
    r_affinetridesc.pskindesc = pskindesc;
    r_affinetridesc.pskin = reinterpret_cast<void*>(reinterpret_cast<byte*>(paliashdr) + pskindesc->skin);
    r_affinetridesc.skinwidth = a_skinwidth;
    r_affinetridesc.seamfixupX16 = (a_skinwidth >> 1) << 16;
    r_affinetridesc.skinheight = pmdl->skinheight;
}

void R_AliasSetupLighting(alight_t* plighting)
{
    r_ambientlight = plighting->ambientlight;
    if (r_ambientlight < LIGHT_MIN) {
        r_ambientlight = LIGHT_MIN;
    }
    r_ambientlight = (255 - r_ambientlight) << VID_CBITS;
    if (r_ambientlight < LIGHT_MIN) {
        r_ambientlight = LIGHT_MIN;
    }
    r_shadelight = static_cast<float>(plighting->shadelight);
    if (r_shadelight < 0) {
        r_shadelight = 0;
    }
    r_shadelight *= VID_GRADES;

    r_plightvec[0] = DotProduct(plighting->plightvec, alias_forward);
    r_plightvec[1] = -DotProduct(plighting->plightvec, alias_right);
    r_plightvec[2] = DotProduct(plighting->plightvec, alias_up);
}

void R_AliasSetupFrame()
{
    int frame = currententity->frame;
    if ((frame >= pmdl->numframes) || (frame < 0)) {
        Con_DPrintf("R_AliasSetupFrame: no such frame %d\n", frame);
        frame = 0;
    }
    if (paliashdr->frames[frame].type == aliasframetype_t::ALIAS_SINGLE) {
        r_apverts = reinterpret_cast<trivertx_t*>(reinterpret_cast<byte*>(paliashdr) + paliashdr->frames[frame].frame);
        return;
    }
    maliasgroup_t* paliasgroup = reinterpret_cast<maliasgroup_t*>(reinterpret_cast<byte*>(paliashdr) + paliashdr->frames[frame].frame);
    float* pintervals = reinterpret_cast<float*>(reinterpret_cast<byte*>(paliashdr) + paliasgroup->intervals);
    int numframes = paliasgroup->numframes;
    float fullinterval = pintervals[numframes - 1];
    float time = static_cast<float>(cl.time + currententity->syncbase);
    float targettime = time - ((int)(time / fullinterval)) * fullinterval;
    int i = 0;
    for (; i < (numframes - 1); i++) {
        if (pintervals[i] > targettime) {
            break;
        }
    }
    r_apverts = reinterpret_cast<trivertx_t*>(reinterpret_cast<byte*>(paliashdr) + paliasgroup->frames[i].frame);
}

void R_AliasDrawModel(alight_t* plighting)
{
    finalvert_t finalverts[MAXALIASVERTS + ((CACHE_SIZE - 1) / sizeof(finalvert_t)) + 1];
    auxvert_t auxverts[MAXALIASVERTS];
    r_amodels_drawn++;

    pfinalverts = (finalvert_t*)(((size_t)&finalverts[0] + CACHE_SIZE - 1) & ~(size_t)(CACHE_SIZE - 1));
    pauxverts = &auxverts[0];
    paliashdr = (aliashdr_t*)Mod_Extradata(currententity->model);
    pmdl = reinterpret_cast<mdl_t*>(reinterpret_cast<byte*>(paliashdr) + paliashdr->model);

    R_AliasSetupSkin();
    R_AliasSetUpTransform(currententity->trivial_accept);
    R_AliasSetupLighting(plighting);
    R_AliasSetupFrame();

    if (!currententity->colormap) {
        Sys_Error("R_AliasDrawModel: !currententity->colormap");
    }
    r_affinetridesc.drawtype = (currententity->trivial_accept == 3) && r_recursiveaffinetriangles;
    if (r_affinetridesc.drawtype) {
        D_PolysetUpdateTables();
    }
    acolormap = currententity->colormap;
    if (currententity != &cl.viewent) {
        ziscale = (float)0x8000 * (float)0x10000;
    } else {
        ziscale = (float)0x8000 * (float)0x10000 * 3.0f;
    }
    if (currententity->trivial_accept) {
        R_AliasPrepareUnclippedPoints();
    } else {
        R_AliasPreparePoints();
    }
}

} // namespace Render
