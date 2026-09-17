// sw_renderer.hpp -- Software Rasterizer implementation of IRenderer
#pragma once

#include "render/renderer.hpp"
#include "render/draw2d.hpp"
#include "render/software/sw_local.hpp"
#include "render/software/sw_main.hpp"
namespace Render {

class SoftwareRenderer final : public IRenderer {
public:
    void Init() override {
        R_Init();
    }

    void Shutdown() override {}

    void BeginFrame() override {}

    void EndFrame() override {}

    void RenderView(const refdef_t& refdef) override {
        r_refdef = refdef;
        R_RenderView();
    }

    void NewMap() override {
        R_NewMap();
    }

    void DrawPic(int x, int y, qpic_t* pic) override {
        Draw::Draw_Pic(x, y, pic);
    }

    void DrawTransPic(int x, int y, qpic_t* pic) override {
        Draw::Draw_TransPic(x, y, pic);
    }

    void DrawCharacter(int x, int y, int num) override {
        Draw::Draw_Character(x, y, num);
    }

    void DrawFill(int x, int y, int w, int h, int c) override {
        Draw::Draw_Fill(x, y, w, h, c);
    }

    void TileClear(int x, int y, int w, int h) override {
        Draw::Draw_TileClear(x, y, w, h);
    }
};

} // namespace Render
