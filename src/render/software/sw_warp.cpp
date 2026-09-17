// sw_warp.cpp -- Underwater Turbulent Warp and Sky Span Software Rasterizers
#include "render/software/sw_warp.hpp"
#include "ui/screen.hpp"
#include "client/client_types.hpp"
#include "render/software/sw_vid.hpp"

#include <cmath>

namespace Render {

std::array<int, SIN_BUFFER_SIZE> sintable{};
std::array<int, SIN_BUFFER_SIZE> intsintable{};

unsigned char* r_turb_pbase = nullptr;
unsigned char* r_turb_pdest = nullptr;
fixed16_t r_turb_s = 0, r_turb_t = 0, r_turb_sstep = 0, r_turb_tstep = 0;
int* r_turb_turb = nullptr;
int r_turb_spancount = 0;

constexpr int SKY_SPAN_SHIFT = 5;
constexpr int SKY_SPAN_MAX = 1 << SKY_SPAN_SHIFT;

void R_InitTurb()
{
    for (int i = 0; i < SIN_BUFFER_SIZE; i++) {
        sintable[i] = static_cast<int>(AMP + std::sin(i * 3.14159 * 2 / CYCLE) * AMP);
        intsintable[i] = static_cast<int>(AMP2 + std::sin(i * 3.14159 * 2 / CYCLE) * AMP2);
    }
}

void D_DrawTurbulent8Span()
{
    do {
        int sturb = ((r_turb_s + r_turb_turb[(r_turb_t >> 16) & (CYCLE - 1)]) >> 16) & 63;
        int tturb = ((r_turb_t + r_turb_turb[(r_turb_s >> 16) & (CYCLE - 1)]) >> 16) & 63;
        *r_turb_pdest++ = *(r_turb_pbase + (tturb << 6) + sturb);
        r_turb_s += r_turb_sstep;
        r_turb_t += r_turb_tstep;
    } while (--r_turb_spancount > 0);
}

void D_WarpScreen()
{
    std::array<byte*, MAXHEIGHT + (AMP2 * 2)> rowptr{};
    std::array<int, MAXWIDTH + (AMP2 * 2)> column{};
    const auto& scr_vrect = Screen::GetScreenSystem().GetVrect();
    int w = r_refdef.vrect.width;
    int h = r_refdef.vrect.height;
    float wratio = static_cast<float>(w) / static_cast<float>(scr_vrect.width);
    float hratio = static_cast<float>(h) / static_cast<float>(scr_vrect.height);

    for (int v = 0; v < scr_vrect.height + AMP2 * 2; v++) {
        rowptr[v] = reinterpret_cast<byte*>(d_viewbuffer) + (r_refdef.vrect.y * screenwidth) +
            (screenwidth * static_cast<int>(static_cast<float>(v) * hratio * static_cast<float>(h) / static_cast<float>(h + AMP2 * 2)));
    }
    for (int u = 0; u < scr_vrect.width + AMP2 * 2; u++) {
        column[u] = r_refdef.vrect.x + static_cast<int>(static_cast<float>(u) * wratio * static_cast<float>(w) / static_cast<float>(w + AMP2 * 2));
    }
    int* turb = intsintable.data() + (static_cast<int>(Client::cl.time * SPEED) & (CYCLE - 1));
    byte* dest = reinterpret_cast<byte*>(Vid::vid.buffer) + scr_vrect.y * Vid::vid.rowbytes + scr_vrect.x;
    for (int v = 0; v < scr_vrect.height; v++, dest += Vid::vid.rowbytes) {
        int* col = &column[turb[v]];
        byte** row = &rowptr[v];
        for (int u = 0; u < scr_vrect.width; u += 4) {
            dest[u + 0] = row[turb[u + 0]][col[u + 0]];
            dest[u + 1] = row[turb[u + 1]][col[u + 1]];
            dest[u + 2] = row[turb[u + 2]][col[u + 2]];
            dest[u + 3] = row[turb[u + 3]][col[u + 3]];
        }
    }
}

void Turbulent8(espan_t* pspan)
{
    float sdivz16stepu = d_sdivzstepu * 16;
    float tdivz16stepu = d_tdivzstepu * 16;
    float zi16stepu = d_zistepu * 16;
    r_turb_turb = sintable.data() + (static_cast<int>(Client::cl.time * SPEED) & (CYCLE - 1));
    r_turb_sstep = 0;
    r_turb_tstep = 0;
    r_turb_pbase = reinterpret_cast<unsigned char*>(cacheblock);

    do {
        r_turb_pdest = reinterpret_cast<unsigned char*>(reinterpret_cast<byte*>(d_viewbuffer) + (screenwidth * pspan->v) + pspan->u);
        int count = pspan->count;
        float du = static_cast<float>(pspan->u);
        float dv = static_cast<float>(pspan->v);
        float sdivz = d_sdivzorigin + dv * d_sdivzstepv + du * d_sdivzstepu;
        float tdivz = d_tdivzorigin + dv * d_tdivzstepv + du * d_tdivzstepu;
        float zi = d_ziorigin + dv * d_zistepv + du * d_zistepu;
        float z = 0x10000 / zi;
        r_turb_s = static_cast<int>(sdivz * z) + sadjust;
        if (r_turb_s > bbextents) {
            r_turb_s = bbextents;
        } else if (r_turb_s < 0) {
            r_turb_s = 0;
        }
        r_turb_t = static_cast<int>(tdivz * z) + tadjust;
        if (r_turb_t > bbextentt) {
            r_turb_t = bbextentt;
        } else if (r_turb_t < 0) {
            r_turb_t = 0;
        }

        do {
            if (count >= 16) {
                r_turb_spancount = 16;
            } else {
                r_turb_spancount = count;
            }
            count -= r_turb_spancount;
            fixed16_t snext, tnext;
            if (count) {
                sdivz += sdivz16stepu;
                tdivz += tdivz16stepu;
                zi += zi16stepu;
                z = 0x10000 / zi;
                snext = static_cast<int>(sdivz * z) + sadjust;
                if (snext > bbextents) {
                    snext = bbextents;
                } else if (snext < 16) {
                    snext = 16;
                }
                tnext = static_cast<int>(tdivz * z) + tadjust;
                if (tnext > bbextentt) {
                    tnext = bbextentt;
                } else if (tnext < 16) {
                    tnext = 16;
                }
                r_turb_sstep = (snext - r_turb_s) >> 4;
                r_turb_tstep = (tnext - r_turb_t) >> 4;
            } else {
                float spancountminus1 = static_cast<float>(r_turb_spancount - 1);
                sdivz += d_sdivzstepu * spancountminus1;
                tdivz += d_tdivzstepu * spancountminus1;
                zi += d_zistepu * spancountminus1;
                z = 0x10000 / zi;
                snext = static_cast<int>(sdivz * z) + sadjust;
                if (snext > bbextents) {
                    snext = bbextents;
                } else if (snext < 16) {
                    snext = 16;
                }
                tnext = static_cast<int>(tdivz * z) + tadjust;
                if (tnext > bbextentt) {
                    tnext = bbextentt;
                } else if (tnext < 16) {
                    tnext = 16;
                }
                if (r_turb_spancount > 1) {
                    r_turb_sstep = (snext - r_turb_s) / (r_turb_spancount - 1);
                    r_turb_tstep = (tnext - r_turb_t) / (r_turb_spancount - 1);
                }
            }
            r_turb_s = r_turb_s & ((CYCLE << 16) - 1);
            r_turb_t = r_turb_t & ((CYCLE << 16) - 1);
            D_DrawTurbulent8Span();
            r_turb_s = snext;
            r_turb_t = tnext;
        } while (count > 0);
    } while ((pspan = pspan->pnext) != nullptr);
}

void D_Sky_uv_To_st(int u, int v, fixed16_t* s, fixed16_t* t)
{
    float temp = (r_refdef.vrect.width >= r_refdef.vrect.height) ?
        static_cast<float>(r_refdef.vrect.width) : static_cast<float>(r_refdef.vrect.height);
    float wu = 8192.0f * static_cast<float>(u - (static_cast<int>(Vid::vid.width) >> 1)) / temp;
    float wv = 8192.0f * static_cast<float>((static_cast<int>(Vid::vid.height) >> 1) - v) / temp;
    Vector3 end = vpn * 4096.0f + vright * wu + vup * wv;
    end.z *= 3.0f;
    end.normalize();
    temp = skytime * skyspeed;
    *s = static_cast<int>((temp + 6.0f * (static_cast<float>(SKYSIZE) / 2.0f - 1.0f) * end.x) * 65536.0f);
    *t = static_cast<int>((temp + 6.0f * (static_cast<float>(SKYSIZE) / 2.0f - 1.0f) * end.y) * 65536.0f);
}

void D_DrawSkyScans8(espan_t* pspan)
{
    fixed16_t sstep = 0, tstep = 0;
    do {
        unsigned char* pdest = reinterpret_cast<unsigned char*>(reinterpret_cast<byte*>(d_viewbuffer) + (screenwidth * pspan->v) + pspan->u);
        int count = pspan->count;
        int u = pspan->u;
        int v = pspan->v;
        fixed16_t s, t, snext = 0, tnext = 0;
        D_Sky_uv_To_st(u, v, &s, &t);
        do {
            int spancount = (count >= SKY_SPAN_MAX) ? SKY_SPAN_MAX : count;
            count -= spancount;
            if (count) {
                u += spancount;
                D_Sky_uv_To_st(u, v, &snext, &tnext);
                sstep = (snext - s) >> SKY_SPAN_SHIFT;
                tstep = (tnext - t) >> SKY_SPAN_SHIFT;
            } else {
                float spancountminus1 = static_cast<float>(spancount - 1);
                if (spancountminus1 > 0) {
                    u += static_cast<int>(spancountminus1);
                    D_Sky_uv_To_st(u, v, &snext, &tnext);
                    sstep = static_cast<fixed16_t>((snext - s) / spancountminus1);
                    tstep = static_cast<fixed16_t>((tnext - t) / spancountminus1);
                }
            }
            do {
                *pdest++ = r_skysource[((t & R_SKY_TMASK) >> 8) + ((s & R_SKY_SMASK) >> 16)];
                s += sstep;
                t += tstep;
            } while (--spancount > 0);
            s = snext;
            t = tnext;
        } while (count > 0);
    } while ((pspan = pspan->pnext) != nullptr);
}

} // namespace Render
