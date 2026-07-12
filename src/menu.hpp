// menu.h -- menu system declarations
#pragma once
#define MNET_IPX 1
#define MNET_TCP 2

extern int m_activenet;

namespace Menu {

void M_Init(void);
void M_Keydown(int key);
void M_Draw(void);
void M_ToggleMenu_f(void);
void M_Menu_Main_f(void);
void M_Menu_Quit_f(void);

void M_DrawPic(int x, int y, const qpic_t* pic);

extern int m_state;
extern int m_return_state;
extern qboolean m_return_onerror;
extern char m_return_reason[32];

} // namespace Menu
