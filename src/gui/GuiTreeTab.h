#pragma once

class RendererSystems;

namespace GuiTreeTab {
    // Takes RendererSystems because TreeSystem and TreeLODSystem are created
    // late (deferred world content); they must be looked up every frame.
    void render(RendererSystems& systems);
}
