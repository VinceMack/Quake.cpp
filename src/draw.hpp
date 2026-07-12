// draw.h -- these are the only functions outside the refresh allowed
#pragma once
// to touch the vid buffer

namespace Draw {

extern qpic_t* draw_disc;

void Draw_Init(void);
void Draw_Character(int x, int y, int num);
void Draw_Pic(int x, int y, const qpic_t* pic);
void Draw_TransPic(int x, int y, const qpic_t* pic);
void Draw_TransPicTranslate(int x, int y, const qpic_t* pic, const byte* translation);
void Draw_ConsoleBackground(int lines);
void Draw_BeginDisc(void);
void Draw_EndDisc(void);
void Draw_TileClear(int x, int y, int w, int h);
void Draw_Fill(int x, int y, int w, int h, int c);
void Draw_FadeScreen(void);
void Draw_String(int x, int y, const char* str);
inline qpic_t* Draw_PicFromWad(const char* name)
{
    return (qpic_t*)Wad::W_GetLumpName(name);
}
qpic_t* Draw_CachePic(const char* path);

} // namespace Draw
