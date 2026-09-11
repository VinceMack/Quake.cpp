// sys_render.hpp -- Subsystem Render Umbrella Header
#pragma once

#include "render/render_types.hpp"
#include "render/software/sw_local.hpp"
#include "render/renderer.hpp"
#include "render/draw2d.hpp"
#include "render/software/sw_vid.hpp"
#include "client/screen.hpp"
#include "client/view.hpp"
#include "ui/hud.hpp"
#include "render/software/sw_part.hpp"
#include "render/software/sw_sky.hpp"
#include "render/software/sw_light.hpp"
#include "render/software/sw_surf.hpp"
#include "render/software/sw_drawface.hpp"
#include "render/software/sw_edge.hpp"
#include "render/software/sw_bsp.hpp"
#include "render/software/sw_sprite.hpp"
#include "render/software/sw_alias_clip.hpp"
#include "render/software/sw_alias.hpp"
#include "render/software/sw_warp.hpp"
#include "render/software/sw_poly.hpp"
#include "render/software/sw_sprite_draw.hpp"
#include "render/software/sw_raster.hpp"
#include "render/software/sw_efrag.hpp"
#include "render/software/sw_frame.hpp"
#include "render/software/sw_main.hpp"
#include "render/software/sw_renderer.hpp"

namespace Render {

inline void D_EnableBackBufferAccess() { VID_LockBuffer(); }
inline void D_DisableBackBufferAccess() { VID_UnlockBuffer(); }

} // namespace Render
