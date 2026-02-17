#pragma once

#include "ModelLoader.h"
#include "CALMLowLevelController.h"
#include "CALMHighLevelController.h"
#include "CALMLatentSpace.h"
#include "CALMCharacterConfig.h"
#include <string>

namespace ml {

// Loads all CALM model components from a directory exported by calm_export.py.
//
// Expected directory layout:
//   <dir>/llc_style.bin       - Style MLP weights
//   <dir>/llc_main.bin        - Main policy MLP weights
//   <dir>/llc_mu_head.bin     - Action head weights
//   <dir>/encoder.bin         - Motion encoder (optional)
//   <dir>/hlc_heading.bin     - Heading HLC (optional)
//   <dir>/hlc_location.bin    - Location HLC (optional)
//   <dir>/hlc_strike.bin      - Strike HLC (optional)
//   <dir>/latent_library.json - Pre-encoded behavior latents (optional)
//   <dir>/retarget_map.json   - Skeleton joint retargeting map (optional)
//
// Usage:
//   CALMLowLevelController llc;
//   CALMLatentSpace latentSpace(64);
//   if (CALMModelLoader::loadLLC("data/calm/models", llc)) { ... }
//   if (CALMModelLoader::loadLatentLibrary("data/calm/models", latentSpace)) { ... }
class CALMModelLoader {
public:
    // Load the LLC (style MLP + main MLP + mu head) from three .bin files.
    static bool loadLLC(const std::string& modelDir, CALMLowLevelController& llc);

    // Load the encoder network into a latent space.
    static bool loadEncoder(const std::string& modelDir, CALMLatentSpace& latentSpace);

    // Load the latent library JSON into a latent space.
    static bool loadLatentLibrary(const std::string& modelDir, CALMLatentSpace& latentSpace);

    // Load an HLC from a .bin file.
    // taskName: "heading", "location", "strike", etc.
    static bool loadHLC(const std::string& modelDir, const std::string& taskName,
                        CALMHighLevelController& hlc);

    // Load a skeleton retarget map from JSON.
    // Returns a map from training joint names to engine joint names.
    // Retarget map JSON format:
    //   {
    //     "training_to_engine_joint_map": { "pelvis": "Hips", ... },
    //     "scale_factor": 1.0
    //   }
    struct RetargetMap {
        std::unordered_map<std::string, std::string> jointMap;
        float scaleFactor = 1.0f;
    };
    static bool loadRetargetMap(const std::string& path, RetargetMap& map);

    // Convenience: load everything from a model directory.
    // Returns true if at least the LLC loads successfully.
    struct CALMModelSet {
        CALMLowLevelController llc;
        CALMLatentSpace latentSpace;
        CALMHighLevelController headingHLC;
        CALMHighLevelController locationHLC;
        CALMHighLevelController strikeHLC;
        bool hasEncoder = false;
        bool hasLibrary = false;
        bool hasHeadingHLC = false;
        bool hasLocationHLC = false;
        bool hasStrikeHLC = false;
    };
    static bool loadAll(const std::string& modelDir, CALMModelSet& models, int latentDim = 64);
};

} // namespace ml
