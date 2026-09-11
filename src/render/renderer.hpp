// renderer.hpp -- Abstract IRenderer interface for rendering backends (Software, SDL_GPU)
#pragma once

#include <cstdint>
#include "sys_core.hpp"

struct refdef_t;
struct qpic_t;

namespace Render {

class IRenderer {
public:
    virtual ~IRenderer() = default;

    // Subsystem lifecycle
    virtual void Init() = 0;
    virtual void Shutdown() = 0;

    // Frame management
    virtual void BeginFrame() = 0;
    virtual void EndFrame() = 0;

    // Scene / World rendering
    virtual void RenderView(const refdef_t& refdef) = 0;
    virtual void NewMap() = 0;

    // 2D primitives
    virtual void DrawPic(int x, int y, qpic_t* pic) = 0;
    virtual void DrawTransPic(int x, int y, qpic_t* pic) = 0;
    virtual void DrawCharacter(int x, int y, int num) = 0;
    virtual void DrawFill(int x, int y, int w, int h, int c) = 0;
    virtual void TileClear(int x, int y, int w, int h) = 0;
};

// Global renderer instance access
IRenderer* GetRenderer();
void SetRenderer(IRenderer* renderer);

} // namespace Render
