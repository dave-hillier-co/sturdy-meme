#include "TerrainTileDiskLoader.h"
#include <SDL3/SDL.h>
#include <stb_image.h>
#include <sstream>

std::unique_ptr<TerrainTileDiskLoader> TerrainTileDiskLoader::create(
    const std::string& cacheDirectory,
    uint32_t tileResolution,
    uint32_t storedTileResolution,
    uint32_t workerCount) {
    auto loader = std::make_unique<TerrainTileDiskLoader>(ConstructToken{});
    if (!loader->initInternal(cacheDirectory, tileResolution, storedTileResolution, workerCount)) {
        return nullptr;
    }
    return loader;
}

TerrainTileDiskLoader::~TerrainTileDiskLoader() {
    cleanup();
}

bool TerrainTileDiskLoader::initInternal(const std::string& cacheDirectory,
                                         uint32_t tileResolution,
                                         uint32_t storedTileResolution,
                                         uint32_t workerCount) {
    cacheDirectory_ = cacheDirectory;
    tileResolution_ = tileResolution;
    storedTileResolution_ = storedTileResolution;
    running_ = true;

    if (workerCount < 1) workerCount = 1;
    workers_.reserve(workerCount);
    for (uint32_t i = 0; i < workerCount; ++i) {
        workers_.emplace_back(&TerrainTileDiskLoader::workerLoop, this);
    }

    SDL_Log("TerrainTileDiskLoader: %u workers, path: %s", workerCount, cacheDirectory_.c_str());
    return true;
}

void TerrainTileDiskLoader::cleanup() {
    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        running_ = false;
    }
    queueCondition_.notify_all();

    for (auto& worker : workers_) {
        if (worker.joinable()) {
            worker.join();
        }
    }
    workers_.clear();

    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        while (!requestQueue_.empty()) requestQueue_.pop();
        inFlight_.clear();
    }
    {
        std::lock_guard<std::mutex> lock(completedMutex_);
        completed_.clear();
    }
}

void TerrainTileDiskLoader::queueTile(TileCoord coord, uint32_t lod, int priority) {
    uint64_t key = makeKey(coord, lod);
    std::lock_guard<std::mutex> lock(queueMutex_);
    if (inFlight_.find(key) != inFlight_.end()) {
        return;  // already queued or being decoded
    }
    requestQueue_.push(LoadRequest{coord, lod, priority});
    inFlight_.insert(key);
    queueCondition_.notify_one();
}

bool TerrainTileDiskLoader::isPending(TileCoord coord, uint32_t lod) const {
    std::lock_guard<std::mutex> lock(queueMutex_);
    return inFlight_.find(makeKey(coord, lod)) != inFlight_.end();
}

std::vector<TerrainTileDiskLoader::LoadedTile> TerrainTileDiskLoader::drainCompleted() {
    std::lock_guard<std::mutex> lock(completedMutex_);
    std::vector<LoadedTile> result = std::move(completed_);
    completed_.clear();
    return result;
}

uint32_t TerrainTileDiskLoader::getPendingCount() const {
    std::lock_guard<std::mutex> lock(queueMutex_);
    return static_cast<uint32_t>(requestQueue_.size());
}

void TerrainTileDiskLoader::workerLoop() {
    while (true) {
        LoadRequest request;
        {
            std::unique_lock<std::mutex> lock(queueMutex_);
            queueCondition_.wait(lock, [this] {
                return !running_ || !requestQueue_.empty();
            });
            if (!running_ && requestQueue_.empty()) {
                return;
            }
            if (requestQueue_.empty()) {
                continue;
            }
            request = requestQueue_.top();
            requestQueue_.pop();
            // Keep the key in inFlight_ while decoding so duplicate requests are
            // suppressed; it is cleared only after the result is published (or
            // the load fails), preventing a window where the same tile gets
            // queued and decoded twice concurrently.
        }

        LoadedTile tile;
        bool ok = loadTileFromDisk(request.coord, request.lod, tile);

        uint64_t key = makeKey(request.coord, request.lod);
        if (ok) {
            {
                std::lock_guard<std::mutex> lock(completedMutex_);
                completed_.push_back(std::move(tile));
            }
        } else {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "TerrainTileDiskLoader: failed to load tile (%d, %d) LOD%u",
                        request.coord.x, request.coord.z, request.lod);
        }

        {
            std::lock_guard<std::mutex> lock(queueMutex_);
            inFlight_.erase(key);
        }
    }
}

std::string TerrainTileDiskLoader::getTilePath(TileCoord coord, uint32_t lod) const {
    std::ostringstream oss;
    oss << cacheDirectory_ << "/tile_" << coord.x << "_" << coord.z << "_lod" << lod << ".png";
    return oss.str();
}

bool TerrainTileDiskLoader::loadTileFromDisk(TileCoord coord, uint32_t lod, LoadedTile& out) const {
    std::string path = getTilePath(coord, lod);

    int width, height, channels;
    uint16_t* data = stbi_load_16(path.c_str(), &width, &height, &channels, 1);
    if (!data) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "TerrainTileDiskLoader: Failed to load tile: %s",
                    path.c_str());
        return false;
    }

    uint32_t loadedRes = static_cast<uint32_t>(width);
    bool isOldFormat = (loadedRes == tileResolution_);
    bool isNewFormat = (loadedRes == storedTileResolution_);

    if ((!isOldFormat && !isNewFormat) || width != height) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "TerrainTileDiskLoader: Tile %s is %dx%d, expected square %ux%u or %ux%u",
                     path.c_str(), width, height, tileResolution_, tileResolution_,
                     storedTileResolution_, storedTileResolution_);
        stbi_image_free(data);
        return false;
    }

    out.coord = coord;
    out.lod = lod;
    out.resolution = loadedRes;
    out.cpuData.resize(static_cast<size_t>(loadedRes) * loadedRes);
    for (size_t i = 0; i < out.cpuData.size(); ++i) {
        out.cpuData[i] = static_cast<float>(data[i]) / 65535.0f;
    }

    stbi_image_free(data);
    return true;
}
