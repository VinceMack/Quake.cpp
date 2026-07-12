// vid.h -- video driver defs
#pragma once

#define VID_CBITS 6
#define VID_GRADES (1 << VID_CBITS)

// a pixel can be one, two, or four bytes
typedef byte pixel_t;

typedef struct vrect_s {
    int x, y, width, height;
    struct vrect_s* pnext;
} vrect_t;

typedef struct {
    pixel_t* buffer;            // invisible buffer
    pixel_t* colormap;          // 256 * VID_GRADES size
    unsigned short* colormap16; // 256 * VID_GRADES size
    int fullbright;             // index of first fullbright color
    unsigned rowbytes;          // may be > width if displayed in a window
    unsigned width;
    unsigned height;
    float aspect; // width / height -- < 0 is taller than wide
    int numpages;
    int recalc_refdef; // if true, recalc vid-based stuff
    pixel_t* conbuffer;
    int conrowbytes;
    unsigned conwidth;
    unsigned conheight;
    int maxwarpwidth;
    int maxwarpheight;
    pixel_t* direct; // direct drawing to framebuffer, if not
                     //  NULL
} viddef_t;

namespace Vid {

extern viddef_t vid;
extern unsigned short d_8to16table[256];
extern unsigned d_8to24table[256];
extern void (*vid_menudrawfn)(void);
extern void (*vid_menukeyfn)(int key);

void VID_SetPalette(const unsigned char* palette);

inline void VID_ShiftPalette(const unsigned char* palette)
{
    VID_SetPalette(palette);
}

void VID_Init(const unsigned char* palette);

void VID_Shutdown(void);

void VID_Update(const vrect_t* rects);

int VID_SetMode(int modenum, unsigned char* palette);

void VID_HandlePause();

void D_BeginDirectRect(int x, int y, byte* pbitmap, int width, int height);
void D_EndDirectRect(int x, int y, int width, int height);

} // namespace Vid
