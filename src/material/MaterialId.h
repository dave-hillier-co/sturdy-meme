#pragma once

#include <cstdint>

// Opaque material identifier. Use MaterialRegistry to convert to descriptor sets.
// Lives in its own header so consumers do not need RenderableBuilder.h / Renderable.
using MaterialId = uint32_t;
constexpr MaterialId INVALID_MATERIAL_ID = ~0u;
