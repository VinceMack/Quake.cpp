// sw_sprite_draw.cpp -- Software Sprite and Particle Rasterization
#include "render/software/sw_sprite_draw.hpp"
#include <cmath>

namespace Render {

static int minindex = 0;
static int maxindex = 0;
static int sprite_height = 0;
static sspan_t* sprite_spans = nullptr;

void D_SpriteDrawSpans(sspan_t* pspan)
{
    int count, spancount, izistep;
    int izi;
    byte *pbase, *pdest;
    fixed16_t s, t, snext, tnext, sstep, tstep;
    float sdivz, tdivz, zi, z, du, dv, spancountminus1;
    float sdivz8stepu, tdivz8stepu, zi8stepu;
    byte btemp;
    short* pz;
    sstep = 0;
    tstep = 0;
    pbase = cacheblock;
    sdivz8stepu = d_sdivzstepu * 8;
    tdivz8stepu = d_tdivzstepu * 8;
    zi8stepu = d_zistepu * 8;
    izistep = static_cast<int>(d_zistepu * 0x8000 * 0x10000);
    do {
        pdest = reinterpret_cast<byte*>(d_viewbuffer) + (screenwidth * pspan->v) + pspan->u;
        pz = d_pzbuffer + (d_zwidth * pspan->v) + pspan->u;
        count = pspan->count;
        if (count <= 0) {
            goto NextSpan;
        }
        du = static_cast<float>(pspan->u);
        dv = static_cast<float>(pspan->v);
        sdivz = d_sdivzorigin + dv * d_sdivzstepv + du * d_sdivzstepu;
        tdivz = d_tdivzorigin + dv * d_tdivzstepv + du * d_tdivzstepu;
        zi = d_ziorigin + dv * d_zistepv + du * d_zistepu;
        z = 0x10000 / zi;
        izi = static_cast<int>(zi * 0x8000 * 0x10000);
        s = static_cast<int>(sdivz * z) + sadjust;
        if (s > bbextents) {
            s = bbextents;
        } else if (s < 0) {
            s = 0;
        }
        t = static_cast<int>(tdivz * z) + tadjust;
        if (t > bbextentt) {
            t = bbextentt;
        } else if (t < 0) {
            t = 0;
        }
        do {
            if (count >= 8) {
                spancount = 8;
            } else {
                spancount = count;
            }
            count -= spancount;
            if (count) {
                sdivz += sdivz8stepu;
                tdivz += tdivz8stepu;
                zi += zi8stepu;
                z = 0x10000 / zi;
                snext = static_cast<int>(sdivz * z) + sadjust;
                if (snext > bbextents) {
                    snext = bbextents;
                } else if (snext < 8) {
                    snext = 8;
                }
                tnext = static_cast<int>(tdivz * z) + tadjust;
                if (tnext > bbextentt) {
                    tnext = bbextentt;
                } else if (tnext < 8) {
                    tnext = 8;
                }
                sstep = (snext - s) >> 3;
                tstep = (tnext - t) >> 3;
            } else {
                spancountminus1 = static_cast<float>(spancount - 1);
                sdivz += d_sdivzstepu * spancountminus1;
                tdivz += d_tdivzstepu * spancountminus1;
                zi += d_zistepu * spancountminus1;
                z = 0x10000 / zi;
                snext = static_cast<int>(sdivz * z) + sadjust;
                if (snext > bbextents) {
                    snext = bbextents;
                } else if (snext < 8) {
                    snext = 8;
                }
                tnext = static_cast<int>(tdivz * z) + tadjust;
                if (tnext > bbextentt) {
                    tnext = bbextentt;
                } else if (tnext < 8) {
                    tnext = 8;
                }
                if (spancount > 1) {
                    sstep = (snext - s) / (spancount - 1);
                    tstep = (tnext - t) / (spancount - 1);
                }
            }
            do {
                btemp = *(pbase + (s >> 16) + (t >> 16) * cachewidth);
                if (btemp != 255) {
                    if (*pz <= static_cast<short>(izi >> 16)) {
                        *pz = static_cast<short>(izi >> 16);
                        *pdest = btemp;
                    }
                }
                izi += izistep;
                pdest++;
                pz++;
                s += sstep;
                t += tstep;
            } while (--spancount > 0);
            s = snext;
            t = tnext;
        } while (count > 0);
    NextSpan:
        pspan++;
    } while (pspan->count != DS_SPAN_LIST_END);
}

void D_SpriteScanLeftEdge()
{
    int i, v, itop, ibottom, lmaxindex;
    emitpoint_t *pvert, *pnext;
    sspan_t* pspan;
    float du, dv, vtop, vbottom, slope;
    fixed16_t u, u_step;
    pspan = sprite_spans;
    i = minindex;
    if (i == 0) {
        i = r_spritedesc.nump;
    }
    lmaxindex = maxindex;
    if (lmaxindex == 0) {
        lmaxindex = r_spritedesc.nump;
    }
    vtop = std::ceil(r_spritedesc.pverts[i].v);
    do {
        pvert = &r_spritedesc.pverts[i];
        pnext = pvert - 1;
        vbottom = std::ceil(pnext->v);
        if (vtop < vbottom) {
            du = pnext->u - pvert->u;
            dv = pnext->v - pvert->v;
            slope = du / dv;
            u_step = static_cast<fixed16_t>(slope * 65536.0f);
            u = static_cast<fixed16_t>((pvert->u + (slope * (vtop - pvert->v))) * 65536.0f) + (65536 - 1);
            itop = static_cast<int>(vtop);
            ibottom = static_cast<int>(vbottom);
            for (v = itop; v < ibottom; v++) {
                pspan->u = u >> 16;
                pspan->v = v;
                u += u_step;
                pspan++;
            }
        }
        vtop = vbottom;
        i--;
        if (i == 0) {
            i = r_spritedesc.nump;
        }
    } while (i != lmaxindex);
}

void D_SpriteScanRightEdge()
{
    int i, v, itop, ibottom;
    emitpoint_t *pvert, *pnext;
    sspan_t* pspan;
    float du, dv, vtop, vbottom, slope, uvert, unext, vvert, vnext;
    fixed16_t u, u_step;
    pspan = sprite_spans;
    i = minindex;
    vvert = r_spritedesc.pverts[i].v;
    if (vvert < r_refdef.fvrecty_adj) {
        vvert = r_refdef.fvrecty_adj;
    }
    if (vvert > r_refdef.fvrectbottom_adj) {
        vvert = r_refdef.fvrectbottom_adj;
    }
    vtop = std::ceil(vvert);
    do {
        pvert = &r_spritedesc.pverts[i];
        pnext = pvert + 1;
        vnext = pnext->v;
        if (vnext < r_refdef.fvrecty_adj) {
            vnext = r_refdef.fvrecty_adj;
        }
        if (vnext > r_refdef.fvrectbottom_adj) {
            vnext = r_refdef.fvrectbottom_adj;
        }
        vbottom = std::ceil(vnext);
        if (vtop < vbottom) {
            uvert = pvert->u;
            if (uvert < r_refdef.fvrectx_adj) {
                uvert = r_refdef.fvrectx_adj;
            }
            if (uvert > r_refdef.fvrectright_adj) {
                uvert = r_refdef.fvrectright_adj;
            }
            unext = pnext->u;
            if (unext < r_refdef.fvrectx_adj) {
                unext = r_refdef.fvrectx_adj;
            }
            if (unext > r_refdef.fvrectright_adj) {
                unext = r_refdef.fvrectright_adj;
            }
            du = unext - uvert;
            dv = vnext - vvert;
            slope = du / dv;
            u_step = static_cast<fixed16_t>(slope * 65536.0f);
            u = static_cast<fixed16_t>((uvert + (slope * (vtop - vvert))) * 65536.0f) + (65536 - 1);
            itop = static_cast<int>(vtop);
            ibottom = static_cast<int>(vbottom);
            for (v = itop; v < ibottom; v++) {
                pspan->count = (u >> 16) - pspan->u;
                u += u_step;
                pspan++;
            }
        }
        vtop = vbottom;
        vvert = vnext;
        i++;
        if (i == r_spritedesc.nump) {
            i = 0;
        }
    } while (i != maxindex);
    pspan->count = DS_SPAN_LIST_END;
}

void D_SpriteCalculateGradients()
{
    Vector3 p_normal, p_saxis, p_taxis, p_temp1;
    float distinv;
    TransformVector(r_spritedesc.vpn, p_normal);
    TransformVector(r_spritedesc.vright, p_saxis);
    TransformVector(r_spritedesc.vup, p_taxis);
    p_taxis = -p_taxis;
    distinv = 1.0f / (-modelorg.dot(r_spritedesc.vpn));
    d_sdivzstepu = p_saxis.x * xscaleinv;
    d_tdivzstepu = p_taxis.x * xscaleinv;
    d_sdivzstepv = -p_saxis.y * yscaleinv;
    d_tdivzstepv = -p_taxis.y * yscaleinv;
    d_zistepu = p_normal.x * xscaleinv * distinv;
    d_zistepv = -p_normal.y * yscaleinv * distinv;
    d_sdivzorigin = p_saxis.z - xcenter * d_sdivzstepu - ycenter * d_sdivzstepv;
    d_tdivzorigin = p_taxis.z - xcenter * d_tdivzstepu - ycenter * d_tdivzstepv;
    d_ziorigin = p_normal.z * distinv - xcenter * d_zistepu - ycenter * d_zistepv;
    TransformVector(modelorg, p_temp1);
    sadjust = static_cast<fixed16_t>(p_temp1.dot(p_saxis) * 65536.0f + 0.5f) - (-(cachewidth >> 1) << 16);
    tadjust = static_cast<fixed16_t>(p_temp1.dot(p_taxis) * 65536.0f + 0.5f) - (-(sprite_height >> 1) << 16);
    bbextents = (cachewidth << 16) - 1;
    bbextentt = (sprite_height << 16) - 1;
}

void D_DrawSprite()
{
    int i, nump;
    float ymin, ymax;
    emitpoint_t* pverts;
    std::array<sspan_t, MAXHEIGHT + 1> spans { };
    sprite_spans = spans.data();
    ymin = 999999.9f;
    ymax = -999999.9f;
    pverts = r_spritedesc.pverts;
    for (i = 0; i < r_spritedesc.nump; i++) {
        if (pverts->v < ymin) {
            ymin = pverts->v;
            minindex = i;
        }
        if (pverts->v > ymax) {
            ymax = pverts->v;
            maxindex = i;
        }
        pverts++;
    }
    ymin = std::ceil(ymin);
    ymax = std::ceil(ymax);
    if (ymin >= ymax) {
        return;
    }
    cachewidth = r_spritedesc.pspriteframe->width;
    sprite_height = r_spritedesc.pspriteframe->height;
    cacheblock = reinterpret_cast<byte*>(&r_spritedesc.pspriteframe->pixels[0]);
    nump = r_spritedesc.nump;
    pverts = r_spritedesc.pverts;
    pverts[nump] = pverts[0];
    D_SpriteCalculateGradients();
    D_SpriteScanLeftEdge();
    D_SpriteScanRightEdge();
    D_SpriteDrawSpans(sprite_spans);
}

void D_EndParticles() { }

void D_StartParticles() { }

void D_DrawParticle(particle_t* pparticle)
{
    Vector3 local, transformed;
    float zi;
    byte* pdest;
    short* pz;
    int i, izi, pix, count, u, v;
    local = pparticle->org - r_origin;
    transformed.x = local.dot(r_pright);
    transformed.y = local.dot(r_pup);
    transformed.z = local.dot(r_ppn);
    if (transformed.z < PARTICLE_Z_CLIP) {
        return;
    }
    zi = 1.0f / transformed.z;
    u = static_cast<int>(xcenter + zi * transformed.x + 0.5f);
    v = static_cast<int>(ycenter - zi * transformed.y + 0.5f);
    if ((v > d_vrectbottom_particle) || (u > d_vrectright_particle) || (v < d_vrecty) || (u < d_vrectx)) {
        return;
    }
    pz = d_pzbuffer + (d_zwidth * v) + u;
    pdest = reinterpret_cast<byte*>(d_viewbuffer) + d_scantable[v] + u;
    izi = static_cast<int>(zi * 32768.0f);
    pix = izi >> d_pix_shift;
    if (pix < d_pix_min) {
        pix = d_pix_min;
    } else if (pix > d_pix_max) {
        pix = d_pix_max;
    }
    switch (pix) {
    case 1:
        count = 1 << d_y_aspect_shift;
        for (; count; count--, pz += d_zwidth, pdest += screenwidth) {
            if (pz[0] <= izi) {
                pz[0] = static_cast<short>(izi);
                pdest[0] = static_cast<byte>(pparticle->color);
            }
        }
        break;
    case 2:
        count = 2 << d_y_aspect_shift;
        for (; count; count--, pz += d_zwidth, pdest += screenwidth) {
            if (pz[0] <= izi) {
                pz[0] = static_cast<short>(izi);
                pdest[0] = static_cast<byte>(pparticle->color);
            }
            if (pz[1] <= izi) {
                pz[1] = static_cast<short>(izi);
                pdest[1] = static_cast<byte>(pparticle->color);
            }
        }
        break;
    case 3:
        count = 3 << d_y_aspect_shift;
        for (; count; count--, pz += d_zwidth, pdest += screenwidth) {
            if (pz[0] <= izi) {
                pz[0] = static_cast<short>(izi);
                pdest[0] = static_cast<byte>(pparticle->color);
            }
            if (pz[1] <= izi) {
                pz[1] = static_cast<short>(izi);
                pdest[1] = static_cast<byte>(pparticle->color);
            }
            if (pz[2] <= izi) {
                pz[2] = static_cast<short>(izi);
                pdest[2] = static_cast<byte>(pparticle->color);
            }
        }
        break;
    case 4:
        count = 4 << d_y_aspect_shift;
        for (; count; count--, pz += d_zwidth, pdest += screenwidth) {
            if (pz[0] <= izi) {
                pz[0] = static_cast<short>(izi);
                pdest[0] = static_cast<byte>(pparticle->color);
            }
            if (pz[1] <= izi) {
                pz[1] = static_cast<short>(izi);
                pdest[1] = static_cast<byte>(pparticle->color);
            }
            if (pz[2] <= izi) {
                pz[2] = static_cast<short>(izi);
                pdest[2] = static_cast<byte>(pparticle->color);
            }
            if (pz[3] <= izi) {
                pz[3] = static_cast<short>(izi);
                pdest[3] = static_cast<byte>(pparticle->color);
            }
        }
        break;
    default:
        count = pix << d_y_aspect_shift;
        for (; count; count--, pz += d_zwidth, pdest += screenwidth) {
            for (i = 0; i < pix; i++) {
                if (pz[i] <= izi) {
                    pz[i] = static_cast<short>(izi);
                    pdest[i] = static_cast<byte>(pparticle->color);
                }
            }
        }
        break;
    }
}

} // namespace Render
