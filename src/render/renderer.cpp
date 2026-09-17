// renderer.cpp -- IRenderer implementation management
#include "render/renderer.hpp"

namespace Render {

static IRenderer* g_renderer = nullptr;

IRenderer* GetRenderer()
{
    return g_renderer;
}

void SetRenderer(IRenderer* renderer)
{
    g_renderer = renderer;
}

} // namespace Render
