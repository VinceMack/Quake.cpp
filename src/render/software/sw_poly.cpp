// sw_poly.cpp -- Alias Model Triangle (Polyset) Rasterization
#include "render/software/sw_poly.hpp"
#include "world/bsp_format.hpp"
#include "core/math.hpp"
#include "quakedef.hpp"

#include <cmath>
#include <tuple>

namespace Render {

constexpr int DPS_MAXSPANS = MAXHEIGHT + 1;

struct edgetable {
    int isflattop = 0;
    int numleftedges = 0;
    const std::array<int, 6>* pleftedgevert0 = nullptr;
    const std::array<int, 6>* pleftedgevert1 = nullptr;
    const std::array<int, 6>* pleftedgevert2 = nullptr;
    int numrightedges = 0;
    const std::array<int, 6>* prightedgevert0 = nullptr;
    const std::array<int, 6>* prightedgevert1 = nullptr;
    const std::array<int, 6>* prightedgevert2 = nullptr;
};

struct spanpackage_t {
    void* pdest = nullptr;
    short* pz = nullptr;
    int count = 0;
    byte* ptex = nullptr;
    int sfrac = 0, tfrac = 0, light = 0, zi = 0;
};

static std::array<int, 6> r_p0{}, r_p1{}, r_p2{};
static byte* d_pcolormap = nullptr;
static int d_xdenom = 0;

static const edgetable edgetables[12] = {
    { 0, 1, &r_p0, &r_p2, nullptr, 2, &r_p0, &r_p1, &r_p2 },
    { 0, 2, &r_p1, &r_p0, &r_p2, 1, &r_p1, &r_p2, nullptr },
    { 1, 1, &r_p0, &r_p2, nullptr, 1, &r_p1, &r_p2, nullptr },
    { 0, 1, &r_p1, &r_p0, nullptr, 2, &r_p1, &r_p2, &r_p0 },
    { 0, 2, &r_p0, &r_p2, &r_p1, 1, &r_p0, &r_p1, nullptr },
    { 0, 1, &r_p2, &r_p1, nullptr, 1, &r_p2, &r_p0, nullptr },
    { 0, 1, &r_p2, &r_p1, nullptr, 2, &r_p2, &r_p0, &r_p1 },
    { 0, 2, &r_p2, &r_p1, &r_p0, 1, &r_p2, &r_p0, nullptr },
    { 0, 1, &r_p1, &r_p0, nullptr, 1, &r_p1, &r_p2, nullptr },
    { 1, 1, &r_p2, &r_p1, nullptr, 1, &r_p0, &r_p1, nullptr },
    { 1, 1, &r_p1, &r_p0, nullptr, 1, &r_p2, &r_p0, nullptr },
    { 0, 1, &r_p0, &r_p2, nullptr, 1, &r_p0, &r_p1, nullptr },
};
static const edgetable* pedgetable = nullptr;

static int a_sstepxfrac = 0, a_tstepxfrac = 0, r_lstepx = 0, a_ststepxwhole = 0;
int r_sstepx = 0, r_tstepx = 0, r_lstepy = 0, r_sstepy = 0, r_tstepy = 0;
int r_zistepx = 0, r_zistepy = 0;
static int d_aspancount = 0, d_countextrastep = 0;
static spanpackage_t* a_spans = nullptr;
static spanpackage_t* d_pedgespanpackage = nullptr;
static int ystart = 0;
static byte* d_pdest = nullptr;
static byte* d_ptex = nullptr;
static short* d_pz = nullptr;
static int d_sfrac = 0, d_tfrac = 0, d_light = 0, d_zi = 0;
static int d_ptexextrastep = 0, d_sfracextrastep = 0;
static int d_tfracextrastep = 0, d_lightextrastep = 0, d_pdestextrastep = 0;
static int d_lightbasestep = 0, d_pdestbasestep = 0, d_ptexbasestep = 0;
static int d_sfracbasestep = 0, d_tfracbasestep = 0;
static int d_ziextrastep = 0, d_zibasestep = 0;
static int d_pzextrastep = 0, d_pzbasestep = 0;
static std::array<byte*, MAX_LBM_HEIGHT> skintable{};
static int skinwidth = 0;
static byte* skinstart = nullptr;

static void D_PolysetRecursiveTriangle(const std::array<int, 6>* p1, const std::array<int, 6>* p2, const std::array<int, 6>* p3);
static void D_PolysetSetEdgeTable();
static void D_RasterizeAliasPolySmooth();
static void D_PolysetScanLeftEdge(int height);
static void D_PolysetCalcGradients(int s_width);
static void D_PolysetDrawSpans8(spanpackage_t* pspanpackage);

void D_PolysetDraw()
{
    alignas(CACHE_SIZE) spanpackage_t spans[DPS_MAXSPANS + 1];
    a_spans = spans;
    if (r_affinetridesc.drawtype) {
        D_DrawSubdiv();
    } else {
        D_DrawNonSubdiv();
    }
}

void D_PolysetDrawFinalVerts(finalvert_t* fv, int num_verts)
{
    for (int i = 0; i < num_verts; i++, fv++) {
        if ((fv->v[0] < r_refdef.vrectright) && (fv->v[1] < r_refdef.vrectbottom)) {
            int z = fv->v[5] >> 16;
            short* zbuf = zspantable[fv->v[1]] + fv->v[0];
            if (z >= *zbuf) {
                *zbuf = static_cast<short>(z);
                int pix = skintable[fv->v[3] >> 16][fv->v[2] >> 16];
                pix = reinterpret_cast<byte*>(acolormap)[pix + (fv->v[4] & 0xFF00)];
                d_viewbuffer[d_scantable[fv->v[1]] + fv->v[0]] = static_cast<pixel_t>(pix);
            }
        }
    }
}

void D_DrawSubdiv()
{
    finalvert_t* pfv = r_affinetridesc.pfinalverts;
    mtriangle_t* ptri = r_affinetridesc.ptriangles;
    int lnumtriangles = r_affinetridesc.numtriangles;

    for (int i = 0; i < lnumtriangles; i++) {
        finalvert_t* index0 = pfv + ptri[i].vertindex[0];
        finalvert_t* index1 = pfv + ptri[i].vertindex[1];
        finalvert_t* index2 = pfv + ptri[i].vertindex[2];

        if (((index0->v[1] - index1->v[1]) * (index0->v[0] - index2->v[0]) -
             (index0->v[0] - index1->v[0]) * (index0->v[1] - index2->v[1])) >= 0) {
            continue;
        }

        d_pcolormap = &reinterpret_cast<byte*>(acolormap)[index0->v[4] & 0xFF00];
        if (ptri[i].facesfront) {
            D_PolysetRecursiveTriangle(&index0->v, &index1->v, &index2->v);
        } else {
            int s0 = index0->v[2];
            int s1 = index1->v[2];
            int s2 = index2->v[2];
            if (index0->flags & ALIAS_ONSEAM) {
                index0->v[2] += r_affinetridesc.seamfixupX16;
            }
            if (index1->flags & ALIAS_ONSEAM) {
                index1->v[2] += r_affinetridesc.seamfixupX16;
            }
            if (index2->flags & ALIAS_ONSEAM) {
                index2->v[2] += r_affinetridesc.seamfixupX16;
            }
            D_PolysetRecursiveTriangle(&index0->v, &index1->v, &index2->v);
            index0->v[2] = s0;
            index1->v[2] = s1;
            index2->v[2] = s2;
        }
    }
}

void D_DrawNonSubdiv()
{
    finalvert_t* pfv = r_affinetridesc.pfinalverts;
    mtriangle_t* ptri = r_affinetridesc.ptriangles;
    int lnumtriangles = r_affinetridesc.numtriangles;

    for (int i = 0; i < lnumtriangles; i++, ptri++) {
        finalvert_t* index0 = pfv + ptri->vertindex[0];
        finalvert_t* index1 = pfv + ptri->vertindex[1];
        finalvert_t* index2 = pfv + ptri->vertindex[2];

        d_xdenom = (index0->v[1] - index1->v[1]) * (index0->v[0] - index2->v[0]) -
                   (index0->v[0] - index1->v[0]) * (index0->v[1] - index2->v[1]);
        if (d_xdenom >= 0) {
            continue;
        }

        r_p0 = index0->v;
        r_p1 = index1->v;
        r_p2 = index2->v;
        if (!ptri->facesfront) {
            if (index0->flags & ALIAS_ONSEAM) {
                r_p0[2] += r_affinetridesc.seamfixupX16;
            }
            if (index1->flags & ALIAS_ONSEAM) {
                r_p1[2] += r_affinetridesc.seamfixupX16;
            }
            if (index2->flags & ALIAS_ONSEAM) {
                r_p2[2] += r_affinetridesc.seamfixupX16;
            }
        }
        D_PolysetSetEdgeTable();
        D_RasterizeAliasPolySmooth();
    }
}

static void D_PolysetRecursiveTriangle(const std::array<int, 6>* lp1,
    const std::array<int, 6>* lp2,
    const std::array<int, 6>* lp3)
{
    const std::array<int, 6>* temp;
    std::array<int, 6> new_poly{};

    int d = (*lp2)[0] - (*lp1)[0];
    if (d < -1 || d > 1) {
        goto split;
    }
    d = (*lp2)[1] - (*lp1)[1];
    if (d < -1 || d > 1) {
        goto split;
    }
    d = (*lp3)[0] - (*lp2)[0];
    if (d < -1 || d > 1) {
        goto split2;
    }
    d = (*lp3)[1] - (*lp2)[1];
    if (d < -1 || d > 1) {
        goto split2;
    }
    d = (*lp1)[0] - (*lp3)[0];
    if (d < -1 || d > 1) {
        goto split3;
    }
    d = (*lp1)[1] - (*lp3)[1];
    if (d < -1 || d > 1) {
    split3:
        temp = lp1;
        lp1 = lp3;
        lp3 = lp2;
        lp2 = temp;
        goto split;
    }
    return;

split2:
    temp = lp1;
    lp1 = lp2;
    lp2 = lp3;
    lp3 = temp;

split:
    new_poly[0] = ((*lp1)[0] + (*lp2)[0]) >> 1;
    new_poly[1] = ((*lp1)[1] + (*lp2)[1]) >> 1;
    new_poly[2] = ((*lp1)[2] + (*lp2)[2]) >> 1;
    new_poly[3] = ((*lp1)[3] + (*lp2)[3]) >> 1;
    new_poly[5] = ((*lp1)[5] + (*lp2)[5]) >> 1;

    if ((*lp2)[1] > (*lp1)[1]) {
        goto nodraw;
    }
    if (((*lp2)[1] == (*lp1)[1]) && ((*lp2)[0] < (*lp1)[0])) {
        goto nodraw;
    }

    {
        int z = new_poly[5] >> 16;
        short* zbuf = zspantable[new_poly[1]] + new_poly[0];
        if (z >= *zbuf) {
            *zbuf = static_cast<short>(z);
            int pix = d_pcolormap[skintable[new_poly[3] >> 16][new_poly[2] >> 16]];
            d_viewbuffer[d_scantable[new_poly[1]] + new_poly[0]] = static_cast<pixel_t>(pix);
        }
    }

nodraw:
    D_PolysetRecursiveTriangle(lp3, lp1, &new_poly);
    D_PolysetRecursiveTriangle(lp3, &new_poly, lp2);
}

void D_PolysetUpdateTables()
{
    if (r_affinetridesc.skinwidth != skinwidth || r_affinetridesc.pskin != skinstart) {
        skinwidth = r_affinetridesc.skinwidth;
        skinstart = reinterpret_cast<byte*>(r_affinetridesc.pskin);
        byte* s = skinstart;
        for (int i = 0; i < MAX_LBM_HEIGHT; i++, s += skinwidth) {
            skintable[i] = s;
        }
    }
}

static void D_PolysetScanLeftEdge(int height)
{
    do {
        d_pedgespanpackage->pdest = d_pdest;
        d_pedgespanpackage->pz = d_pz;
        d_pedgespanpackage->count = d_aspancount;
        d_pedgespanpackage->ptex = d_ptex;
        d_pedgespanpackage->sfrac = d_sfrac;
        d_pedgespanpackage->tfrac = d_tfrac;
        d_pedgespanpackage->light = d_light;
        d_pedgespanpackage->zi = d_zi;
        d_pedgespanpackage++;
        errorterm += erroradjustup;
        if (errorterm >= 0) {
            d_pdest += d_pdestextrastep;
            d_pz += d_pzextrastep;
            d_aspancount += d_countextrastep;
            d_ptex += d_ptexextrastep;
            d_sfrac += d_sfracextrastep;
            d_ptex += d_sfrac >> 16;
            d_sfrac &= 0xFFFF;
            d_tfrac += d_tfracextrastep;
            if (d_tfrac & 0x10000) {
                d_ptex += r_affinetridesc.skinwidth;
                d_tfrac &= 0xFFFF;
            }
            d_light += d_lightextrastep;
            d_zi += d_ziextrastep;
            errorterm -= erroradjustdown;
        } else {
            d_pdest += d_pdestbasestep;
            d_pz += d_pzbasestep;
            d_aspancount += ubasestep;
            d_ptex += d_ptexbasestep;
            d_sfrac += d_sfracbasestep;
            d_ptex += d_sfrac >> 16;
            d_sfrac &= 0xFFFF;
            d_tfrac += d_tfracbasestep;
            if (d_tfrac & 0x10000) {
                d_ptex += r_affinetridesc.skinwidth;
                d_tfrac &= 0xFFFF;
            }
            d_light += d_lightbasestep;
            d_zi += d_zibasestep;
        }
    } while (--height);
}

static void D_PolysetSetUpForLineScan(fixed8_t startvertu,
    fixed8_t startvertv,
    fixed8_t endvertu,
    fixed8_t endvertv)
{
    errorterm = -1;
    int tm = endvertu - startvertu;
    int tn = endvertv - startvertv;
    double dm = static_cast<double>(tm);
    double dn = static_cast<double>(tn);
    std::tie(ubasestep, erroradjustup) = Math::FloorDivMod(dm, dn);
    erroradjustdown = tn;
}

static void D_PolysetCalcGradients(int s_width)
{
    float p00_minus_p20 = static_cast<float>(r_p0[0] - r_p2[0]);
    float p01_minus_p21 = static_cast<float>(r_p0[1] - r_p2[1]);
    float p10_minus_p20 = static_cast<float>(r_p1[0] - r_p2[0]);
    float p11_minus_p21 = static_cast<float>(r_p1[1] - r_p2[1]);
    float xstepdenominv = 1.0f / static_cast<float>(d_xdenom);
    float ystepdenominv = -xstepdenominv;
    float t0 = static_cast<float>(r_p0[4] - r_p2[4]);
    float t1 = static_cast<float>(r_p1[4] - r_p2[4]);
    r_lstepx = static_cast<int>(std::ceil((t1 * p01_minus_p21 - t0 * p11_minus_p21) * xstepdenominv));
    r_lstepy = static_cast<int>(std::ceil((t1 * p00_minus_p20 - t0 * p10_minus_p20) * ystepdenominv));
    t0 = static_cast<float>(r_p0[2] - r_p2[2]);
    t1 = static_cast<float>(r_p1[2] - r_p2[2]);
    r_sstepx = static_cast<int>((t1 * p01_minus_p21 - t0 * p11_minus_p21) * xstepdenominv);
    r_sstepy = static_cast<int>((t1 * p00_minus_p20 - t0 * p10_minus_p20) * ystepdenominv);
    t0 = static_cast<float>(r_p0[3] - r_p2[3]);
    t1 = static_cast<float>(r_p1[3] - r_p2[3]);
    r_tstepx = static_cast<int>((t1 * p01_minus_p21 - t0 * p11_minus_p21) * xstepdenominv);
    r_tstepy = static_cast<int>((t1 * p00_minus_p20 - t0 * p10_minus_p20) * ystepdenominv);
    t0 = static_cast<float>(r_p0[5] - r_p2[5]);
    t1 = static_cast<float>(r_p1[5] - r_p2[5]);
    r_zistepx = static_cast<int>((t1 * p01_minus_p21 - t0 * p11_minus_p21) * xstepdenominv);
    r_zistepy = static_cast<int>((t1 * p00_minus_p20 - t0 * p10_minus_p20) * ystepdenominv);
    a_sstepxfrac = r_sstepx & 0xFFFF;
    a_tstepxfrac = r_tstepx & 0xFFFF;
    a_ststepxwhole = s_width * (r_tstepx >> 16) + (r_sstepx >> 16);
}

static void D_PolysetDrawSpans8(spanpackage_t* pspanpackage)
{
    do {
        int lcount = d_aspancount - pspanpackage->count;
        errorterm += erroradjustup;
        if (errorterm >= 0) {
            d_aspancount += d_countextrastep;
            errorterm -= erroradjustdown;
        } else {
            d_aspancount += ubasestep;
        }
        if (lcount) {
            byte* lpdest = reinterpret_cast<byte*>(pspanpackage->pdest);
            byte* lptex = pspanpackage->ptex;
            short* lpz = pspanpackage->pz;
            int lsfrac = pspanpackage->sfrac;
            int ltfrac = pspanpackage->tfrac;
            int llight = pspanpackage->light;
            int lzi = pspanpackage->zi;
            do {
                if ((lzi >> 16) >= *lpz) {
                    *lpdest = reinterpret_cast<byte*>(acolormap)[*lptex + (llight & 0xFF00)];
                    *lpz = static_cast<short>(lzi >> 16);
                }
                lpdest++;
                lzi += r_zistepx;
                lpz++;
                llight += r_lstepx;
                lptex += a_ststepxwhole;
                lsfrac += a_sstepxfrac;
                lptex += lsfrac >> 16;
                lsfrac &= 0xFFFF;
                ltfrac += a_tstepxfrac;
                if (ltfrac & 0x10000) {
                    lptex += r_affinetridesc.skinwidth;
                    ltfrac &= 0xFFFF;
                }
            } while (--lcount);
        }
        pspanpackage++;
    } while (pspanpackage->count != -999999);
}

static void D_RasterizeAliasPolySmooth()
{
    const std::array<int, 6>* plefttop = pedgetable->pleftedgevert0;
    const std::array<int, 6>* prighttop = pedgetable->prightedgevert0;
    const std::array<int, 6>* pleftbottom = pedgetable->pleftedgevert1;
    const std::array<int, 6>* prightbottom = pedgetable->prightedgevert1;
    int initialleftheight = (*pleftbottom)[1] - (*plefttop)[1];
    int initialrightheight = (*prightbottom)[1] - (*prighttop)[1];

    D_PolysetCalcGradients(r_affinetridesc.skinwidth);
    d_pedgespanpackage = a_spans;
    ystart = (*plefttop)[1];
    d_aspancount = (*plefttop)[0] - (*prighttop)[0];
    d_ptex = reinterpret_cast<byte*>(r_affinetridesc.pskin) + ((*plefttop)[2] >> 16) + ((*plefttop)[3] >> 16) * r_affinetridesc.skinwidth;
    d_sfrac = (*plefttop)[2] & 0xFFFF;
    d_tfrac = (*plefttop)[3] & 0xFFFF;
    d_light = (*plefttop)[4];
    d_zi = (*plefttop)[5];
    d_pdest = reinterpret_cast<byte*>(d_viewbuffer) + ystart * screenwidth + (*plefttop)[0];
    d_pz = d_pzbuffer + ystart * d_zwidth + (*plefttop)[0];

    int working_lstepx;
    if (initialleftheight == 1) {
        d_pedgespanpackage->pdest = d_pdest;
        d_pedgespanpackage->pz = d_pz;
        d_pedgespanpackage->count = d_aspancount;
        d_pedgespanpackage->ptex = d_ptex;
        d_pedgespanpackage->sfrac = d_sfrac;
        d_pedgespanpackage->tfrac = d_tfrac;
        d_pedgespanpackage->light = d_light;
        d_pedgespanpackage->zi = d_zi;
        d_pedgespanpackage++;
    } else {
        D_PolysetSetUpForLineScan((*plefttop)[0], (*plefttop)[1], (*pleftbottom)[0], (*pleftbottom)[1]);
        d_pzbasestep = d_zwidth + ubasestep;
        d_pzextrastep = d_pzbasestep + 1;
        d_pdestbasestep = screenwidth + ubasestep;
        d_pdestextrastep = d_pdestbasestep + 1;
        working_lstepx = (ubasestep < 0) ? r_lstepx - 1 : r_lstepx;
        d_countextrastep = ubasestep + 1;
        d_ptexbasestep = ((r_sstepy + r_sstepx * ubasestep) >> 16) + ((r_tstepy + r_tstepx * ubasestep) >> 16) * r_affinetridesc.skinwidth;
        d_sfracbasestep = (r_sstepy + r_sstepx * ubasestep) & 0xFFFF;
        d_tfracbasestep = (r_tstepy + r_tstepx * ubasestep) & 0xFFFF;
        d_lightbasestep = r_lstepy + working_lstepx * ubasestep;
        d_zibasestep = r_zistepy + r_zistepx * ubasestep;
        d_ptexextrastep = ((r_sstepy + r_sstepx * d_countextrastep) >> 16) + ((r_tstepy + r_tstepx * d_countextrastep) >> 16) * r_affinetridesc.skinwidth;
        d_sfracextrastep = (r_sstepy + r_sstepx * d_countextrastep) & 0xFFFF;
        d_tfracextrastep = (r_tstepy + r_tstepx * d_countextrastep) & 0xFFFF;
        d_lightextrastep = d_lightbasestep + working_lstepx;
        d_ziextrastep = d_zibasestep + r_zistepx;
        D_PolysetScanLeftEdge(initialleftheight);
    }

    if (pedgetable->numleftedges == 2) {
        plefttop = pleftbottom;
        pleftbottom = pedgetable->pleftedgevert2;
        int height = (*pleftbottom)[1] - (*plefttop)[1];
        ystart = (*plefttop)[1];
        d_aspancount = (*plefttop)[0] - (*prighttop)[0];
        d_ptex = reinterpret_cast<byte*>(r_affinetridesc.pskin) + ((*plefttop)[2] >> 16) + ((*plefttop)[3] >> 16) * r_affinetridesc.skinwidth;
        d_sfrac = 0;
        d_tfrac = 0;
        d_light = (*plefttop)[4];
        d_zi = (*plefttop)[5];
        d_pdest = reinterpret_cast<byte*>(d_viewbuffer) + ystart * screenwidth + (*plefttop)[0];
        d_pz = d_pzbuffer + ystart * d_zwidth + (*plefttop)[0];
        if (height == 1) {
            d_pedgespanpackage->pdest = d_pdest;
            d_pedgespanpackage->pz = d_pz;
            d_pedgespanpackage->count = d_aspancount;
            d_pedgespanpackage->ptex = d_ptex;
            d_pedgespanpackage->sfrac = d_sfrac;
            d_pedgespanpackage->tfrac = d_tfrac;
            d_pedgespanpackage->light = d_light;
            d_pedgespanpackage->zi = d_zi;
            d_pedgespanpackage++;
        } else {
            D_PolysetSetUpForLineScan((*plefttop)[0], (*plefttop)[1], (*pleftbottom)[0], (*pleftbottom)[1]);
            d_pdestbasestep = screenwidth + ubasestep;
            d_pdestextrastep = d_pdestbasestep + 1;
            d_pzbasestep = d_zwidth + ubasestep;
            d_pzextrastep = d_pzbasestep + 1;
            working_lstepx = (ubasestep < 0) ? r_lstepx - 1 : r_lstepx;
            d_countextrastep = ubasestep + 1;
            d_ptexbasestep = ((r_sstepy + r_sstepx * ubasestep) >> 16) + ((r_tstepy + r_tstepx * ubasestep) >> 16) * r_affinetridesc.skinwidth;
            d_sfracbasestep = (r_sstepy + r_sstepx * ubasestep) & 0xFFFF;
            d_tfracbasestep = (r_tstepy + r_tstepx * ubasestep) & 0xFFFF;
            d_lightbasestep = r_lstepy + working_lstepx * ubasestep;
            d_zibasestep = r_zistepy + r_zistepx * ubasestep;
            d_ptexextrastep = ((r_sstepy + r_sstepx * d_countextrastep) >> 16) + ((r_tstepy + r_tstepx * d_countextrastep) >> 16) * r_affinetridesc.skinwidth;
            d_sfracextrastep = (r_sstepy + r_sstepx * d_countextrastep) & 0xFFFF;
            d_tfracextrastep = (r_tstepy + r_tstepx * d_countextrastep) & 0xFFFF;
            d_lightextrastep = d_lightbasestep + working_lstepx;
            d_ziextrastep = d_zibasestep + r_zistepx;
            D_PolysetScanLeftEdge(height);
        }
    }

    d_pedgespanpackage = a_spans;
    D_PolysetSetUpForLineScan((*prighttop)[0], (*prighttop)[1], (*prightbottom)[0], (*prightbottom)[1]);
    d_aspancount = 0;
    d_countextrastep = ubasestep + 1;
    int originalcount = a_spans[initialrightheight].count;
    a_spans[initialrightheight].count = -999999;
    D_PolysetDrawSpans8(a_spans);

    if (pedgetable->numrightedges == 2) {
        spanpackage_t* pstart = a_spans + initialrightheight;
        pstart->count = originalcount;
        d_aspancount = (*prightbottom)[0] - (*prighttop)[0];
        prighttop = prightbottom;
        prightbottom = pedgetable->prightedgevert2;
        int height = (*prightbottom)[1] - (*prighttop)[1];
        D_PolysetSetUpForLineScan((*prighttop)[0], (*prighttop)[1], (*prightbottom)[0], (*prightbottom)[1]);
        d_countextrastep = ubasestep + 1;
        a_spans[initialrightheight + height].count = -999999;
        D_PolysetDrawSpans8(pstart);
    }
}

static void D_PolysetSetEdgeTable()
{
    int edgetableindex = 0;
    if (r_p0[1] >= r_p1[1]) {
        if (r_p0[1] == r_p1[1]) {
            if (r_p0[1] < r_p2[1]) {
                pedgetable = &edgetables[2];
            } else {
                pedgetable = &edgetables[5];
            }
            return;
        } else {
            edgetableindex = 1;
        }
    }
    if (r_p0[1] == r_p2[1]) {
        if (edgetableindex) {
            pedgetable = &edgetables[8];
        } else {
            pedgetable = &edgetables[9];
        }
        return;
    } else if (r_p1[1] == r_p2[1]) {
        if (edgetableindex) {
            pedgetable = &edgetables[10];
        } else {
            pedgetable = &edgetables[11];
        }
        return;
    }
    if (r_p0[1] > r_p2[1]) {
        edgetableindex += 2;
    }
    if (r_p1[1] > r_p2[1]) {
        edgetableindex += 4;
    }
    pedgetable = &edgetables[edgetableindex];
}

} // namespace Render
