// draw.hpp -- draw system header
#pragma once

#include "quakedef.hpp"
#include <EASTL/string_view.h>

namespace Draw {

extern qpic_t* draw_disc;

void Draw_Init();
void Draw_Character(int x, int y, int num);
void Draw_Pic(int x, int y, qpic_t* pic);
void Draw_TransPic(int x, int y, qpic_t* pic);
void Draw_TransPicTranslate(int x, int y, qpic_t* pic, const byte* translation);
void Draw_ConsoleBackground(int lines);
void Draw_BeginDisc();
void Draw_EndDisc();
void Draw_TileClear(int x, int y, int w, int h);
void Draw_Fill(int x, int y, int w, int h, int c);
void Draw_FadeScreen();
void Draw_String(int x, int y, eastl::string_view str);

inline qpic_t* Draw_PicFromWad(eastl::string_view name)
{
    return static_cast<qpic_t*>(Wad::W_GetLumpName(name));
}

qpic_t* Draw_CachePic(eastl::string_view path);

} // namespace Draw
