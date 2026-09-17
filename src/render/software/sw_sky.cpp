// sw_sky.cpp -- Dynamic Scrolling Sky Surface Generation
#include "render/software/sw_sky.hpp"
#include "client/client_types.hpp"
#include "core/math.hpp"

using namespace Client;
using namespace Math;

namespace Render {

int iskyspeed = 8;
int iskyspeed2 = 2;
float skyspeed = 0.0f;
float skyspeed2 = 0.0f;
float skytime = 0.0f;

byte* r_skysource = nullptr;
int r_skymade = 0;
int r_skydirect = 0;

static eastl::array<byte, 128 * 131> bottomsky{};
static eastl::array<byte, 128 * 131> bottommask{};
alignas(unsigned) static eastl::array<byte, 128 * 256> newsky{};

void R_InitSky(texture_t* mt)
{
    byte* src = reinterpret_cast<byte*>(mt) + mt->offsets[0];
    for (int i = 0; i < 128; i++) {
        for (int j = 0; j < 128; j++) {
            newsky[(i * 256) + j + 128] = src[i * 256 + j + 128];
        }
    }
    for (int i = 0; i < 128; i++) {
        for (int j = 0; j < 131; j++) {
            if (src[i * 256 + (j & 0x7F)]) {
                bottomsky[(i * 131) + j] = src[i * 256 + (j & 0x7F)];
                bottommask[(i * 131) + j] = 0;
            } else {
                bottomsky[(i * 131) + j] = 0;
                bottommask[(i * 131) + j] = 0xff;
            }
        }
    }
    r_skysource = newsky.data();
}

void R_MakeSky()
{
    static int xlast = -1, ylast = -1;
    int xshift = static_cast<int>(skytime * skyspeed);
    int yshift = static_cast<int>(skytime * skyspeed);
    if ((xshift == xlast) && (yshift == ylast)) {
        return;
    }
    xlast = xshift;
    ylast = yshift;
    unsigned* pnewsky = reinterpret_cast<unsigned*>(newsky.data());
    for (int y = 0; y < SKYSIZE; y++) {
        int baseofs = ((y + yshift) & SKYMASK) * 131;
        for (int x = 0; x < SKYSIZE; x++) {
            int ofs = baseofs + ((x + xshift) & SKYMASK);
            *reinterpret_cast<byte*>(pnewsky) = (*(reinterpret_cast<byte*>(pnewsky) + 128) & bottommask[ofs]) | bottomsky[ofs];
            pnewsky = reinterpret_cast<unsigned*>(reinterpret_cast<byte*>(pnewsky) + 1);
        }
        pnewsky += 128 / sizeof(unsigned);
    }
    r_skymade = 1;
}

void R_SetSkyFrame()
{
    skyspeed = static_cast<float>(iskyspeed);
    skyspeed2 = static_cast<float>(iskyspeed2);
    int g = GreatestCommonDivisor(iskyspeed, iskyspeed2);
    int s1 = iskyspeed / g;
    int s2 = iskyspeed2 / g;
    float temp = static_cast<float>(SKYSIZE * s1 * s2);
    skytime = static_cast<float>(cl.time - ((int)(cl.time / temp) * temp));
    r_skymade = 0;
}

} // namespace Render
