#pragma once

class IPlayerControl;

/**
 * NPC LOD debug panel - shows per-NPC LOD level statistics (Real/Bulk/Virtual)
 * and allows toggling the NPC LOD system.
 */
namespace GuiNPCTab {
    void render(IPlayerControl& playerControl);
}
