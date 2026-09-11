// hud.hpp -- In-game status bar HUD (health, armor, weapons, ammo, scoreboard)
#pragma once

#include <cstdint>

inline constexpr int SBAR_HEIGHT = 24;
extern int sb_lines;

namespace Sbar {

void Sbar_Init();
void Sbar_Changed();
void Sbar_Draw();
void Sbar_IntermissionOverlay();
void Sbar_FinaleOverlay();

} // namespace Sbar
