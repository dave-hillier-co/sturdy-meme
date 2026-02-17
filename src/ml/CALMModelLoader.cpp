#include "CALMModelLoader.h"
#include <SDL3/SDL_log.h>
#include <nlohmann/json.hpp>
#include <fstream>
#include <filesystem>

namespace ml {

static std::string joinPath(const std::string& dir, const std::string& file) {
    if (dir.empty()) return file;
    if (dir.back() == '/' || dir.back() == '\\') return dir + file;
    return dir + "/" + file;
}

bool CALMModelLoader::loadLLC(const std::string& modelDir, CALMLowLevelController& llc) {
    std::string stylePath = joinPath(modelDir, "llc_style.bin");
    std::string mainPath = joinPath(modelDir, "llc_main.bin");
    std::string muHeadPath = joinPath(modelDir, "llc_mu_head.bin");

    StyleConditionedNetwork network;
    if (!ModelLoader::loadStyleConditioned(stylePath, mainPath, network)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "CALMModelLoader: failed to load LLC style/main from %s", modelDir.c_str());
        return false;
    }

    MLPNetwork muHead;
    if (!ModelLoader::loadMLP(muHeadPath, muHead)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "CALMModelLoader: failed to load mu head from %s", muHeadPath.c_str());
        return false;
    }

    llc.setNetwork(std::move(network));
    llc.setMuHead(std::move(muHead));

    SDL_Log("CALMModelLoader: loaded LLC from %s", modelDir.c_str());
    return true;
}

bool CALMModelLoader::loadEncoder(const std::string& modelDir, CALMLatentSpace& latentSpace) {
    std::string encoderPath = joinPath(modelDir, "encoder.bin");

    if (!std::filesystem::exists(encoderPath)) {
        SDL_Log("CALMModelLoader: no encoder.bin found (optional)");
        return false;
    }

    MLPNetwork encoder;
    if (!ModelLoader::loadMLP(encoderPath, encoder)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "CALMModelLoader: failed to load encoder from %s", encoderPath.c_str());
        return false;
    }

    latentSpace.setEncoder(std::move(encoder));
    SDL_Log("CALMModelLoader: loaded encoder from %s", encoderPath.c_str());
    return true;
}

bool CALMModelLoader::loadLatentLibrary(const std::string& modelDir, CALMLatentSpace& latentSpace) {
    std::string libraryPath = joinPath(modelDir, "latent_library.json");

    if (!std::filesystem::exists(libraryPath)) {
        SDL_Log("CALMModelLoader: no latent_library.json found (optional)");
        return false;
    }

    return latentSpace.loadLibraryFromJSON(libraryPath);
}

bool CALMModelLoader::loadHLC(const std::string& modelDir, const std::string& taskName,
                               CALMHighLevelController& hlc) {
    std::string hlcPath = joinPath(modelDir, "hlc_" + taskName + ".bin");

    if (!std::filesystem::exists(hlcPath)) {
        SDL_Log("CALMModelLoader: no hlc_%s.bin found (optional)", taskName.c_str());
        return false;
    }

    MLPNetwork network;
    if (!ModelLoader::loadMLP(hlcPath, network)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "CALMModelLoader: failed to load HLC from %s", hlcPath.c_str());
        return false;
    }

    hlc.setNetwork(std::move(network));
    SDL_Log("CALMModelLoader: loaded HLC '%s' from %s", taskName.c_str(), hlcPath.c_str());
    return true;
}

bool CALMModelLoader::loadRetargetMap(const std::string& path, RetargetMap& map) {
    std::ifstream file(path);
    if (!file.is_open()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "CALMModelLoader: failed to open retarget map %s", path.c_str());
        return false;
    }

    nlohmann::json doc;
    try {
        doc = nlohmann::json::parse(file);
    } catch (const nlohmann::json::parse_error& e) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "CALMModelLoader: JSON parse error in %s: %s", path.c_str(), e.what());
        return false;
    }

    map.scaleFactor = doc.value("scale_factor", 1.0f);

    if (doc.contains("training_to_engine_joint_map") &&
        doc["training_to_engine_joint_map"].is_object()) {
        for (auto& [key, val] : doc["training_to_engine_joint_map"].items()) {
            map.jointMap[key] = val.get<std::string>();
        }
    } else {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "CALMModelLoader: missing 'training_to_engine_joint_map' in %s",
                     path.c_str());
        return false;
    }

    SDL_Log("CALMModelLoader: loaded retarget map from %s (%zu joints, scale=%.2f)",
            path.c_str(), map.jointMap.size(), map.scaleFactor);
    return true;
}

bool CALMModelLoader::loadAll(const std::string& modelDir, CALMModelSet& models, int latentDim) {
    models.latentSpace = CALMLatentSpace(latentDim);

    if (!loadLLC(modelDir, models.llc)) {
        return false;
    }

    models.hasEncoder = loadEncoder(modelDir, models.latentSpace);
    models.hasLibrary = loadLatentLibrary(modelDir, models.latentSpace);
    models.hasHeadingHLC = loadHLC(modelDir, "heading", models.headingHLC);
    models.hasLocationHLC = loadHLC(modelDir, "location", models.locationHLC);
    models.hasStrikeHLC = loadHLC(modelDir, "strike", models.strikeHLC);

    SDL_Log("CALMModelLoader: loaded model set from %s (encoder=%s, library=%s, "
            "heading=%s, location=%s, strike=%s)",
            modelDir.c_str(),
            models.hasEncoder ? "yes" : "no",
            models.hasLibrary ? "yes" : "no",
            models.hasHeadingHLC ? "yes" : "no",
            models.hasLocationHLC ? "yes" : "no",
            models.hasStrikeHLC ? "yes" : "no");
    return true;
}

} // namespace ml
