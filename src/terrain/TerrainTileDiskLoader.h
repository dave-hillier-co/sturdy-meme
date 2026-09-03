#pragma once

#include "TileGridLogic.h"
#include <string>
#include <vector>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <atomic>
#include <unordered_set>
#include <unordered_map>
#include <memory>
#include <cstdint>

using TileCoord = TileGrid::TileCoord;

// Background disk loader for terrain height tiles.
//
// Mirrors VirtualTextureTileLoader: a pool of worker threads pulls the
// highest-priority request off a queue, decodes the 16-bit PNG height tile from
// disk into a normalized float buffer, and pushes the result onto a completed
// list that the main thread drains. Workers never touch TerrainTileCache state
// (loadedTiles, the GPU array, etc.) — they only own their own LoadedTile, so
// there is no shared-mutation hazard with the render thread.
class TerrainTileDiskLoader {
public:
    struct ConstructToken { explicit ConstructToken() = default; };
    explicit TerrainTileDiskLoader(ConstructToken) {}

    // Result of a successful disk load. Self-contained: holds a copy of the
    // decoded height data so the main thread can adopt it without locking.
    struct LoadedTile {
        TileCoord coord;
        uint32_t lod = 0;
        uint32_t resolution = 0;          // width == height of the square tile
        std::vector<float> cpuData;       // normalized [0,1] heights, resolution*resolution
    };

    // Factory. tileResolution / storedTileResolution are used to validate the
    // decoded image size (matching TerrainTileCache's existing checks).
    static std::unique_ptr<TerrainTileDiskLoader> create(
        const std::string& cacheDirectory,
        uint32_t tileResolution,
        uint32_t storedTileResolution,
        uint32_t workerCount = 2);

    ~TerrainTileDiskLoader();

    TerrainTileDiskLoader(const TerrainTileDiskLoader&) = delete;
    TerrainTileDiskLoader& operator=(const TerrainTileDiskLoader&) = delete;
    TerrainTileDiskLoader(TerrainTileDiskLoader&&) = delete;
    TerrainTileDiskLoader& operator=(TerrainTileDiskLoader&&) = delete;

    // Queue a tile for background loading. Lower priority value = loaded sooner
    // (use distance so nearest/highest-LOD tiles win). Duplicate requests for a
    // tile already queued or in-flight are ignored.
    void queueTile(TileCoord coord, uint32_t lod, int priority = 0);

    // True if the tile is currently queued or being decoded by a worker.
    bool isPending(TileCoord coord, uint32_t lod) const;

    // Drain all tiles that have finished decoding. Transfers ownership to caller.
    std::vector<LoadedTile> drainCompleted();

    uint32_t getPendingCount() const;

private:
    bool initInternal(const std::string& cacheDirectory,
                      uint32_t tileResolution,
                      uint32_t storedTileResolution,
                      uint32_t workerCount);
    // Idempotent: signals workers to exit and joins them. Called from the destructor.
    void stop();
    void workerLoop();
    bool loadTileFromDisk(TileCoord coord, uint32_t lod, LoadedTile& out) const;
    std::string getTilePath(TileCoord coord, uint32_t lod) const;

    // Pack coord+lod into a single key for dedup tracking (matches
    // TerrainTileCache::makeTileKey layout).
    static uint64_t makeKey(TileCoord coord, uint32_t lod) {
        return (static_cast<uint64_t>(lod) << 48) |
               (static_cast<uint64_t>(static_cast<uint32_t>(coord.x)) << 24) |
               static_cast<uint64_t>(static_cast<uint32_t>(coord.z));
    }

    struct LoadRequest {
        TileCoord coord;
        uint32_t lod = 0;
        int priority = 0;
        // priority_queue is a max-heap; invert so lowest priority value pops first.
        bool operator<(const LoadRequest& other) const {
            return priority > other.priority;
        }
    };

    std::string cacheDirectory_;
    uint32_t tileResolution_ = 512;
    uint32_t storedTileResolution_ = 513;

    // The destructor joins workers_ explicitly (see stop()) before any member is
    // destroyed, so the relative order of workers_ and the queues below is not
    // load-bearing.
    std::vector<std::thread> workers_;
    std::atomic<bool> running_{false};

    mutable std::mutex queueMutex_;
    std::priority_queue<LoadRequest> requestQueue_;
    std::unordered_set<uint64_t> inFlight_;   // queued or being decoded
    std::condition_variable queueCondition_;

    mutable std::mutex completedMutex_;
    std::vector<LoadedTile> completed_;
};
