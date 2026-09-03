#include "ThreadedTreeGenerator.h"
#include "core/Mesh.h"
#include <SDL3/SDL.h>
#include <glm/gtc/constants.hpp>
#include <cstring>
#include <algorithm>

std::unique_ptr<ThreadedTreeGenerator> ThreadedTreeGenerator::create(uint32_t workerCount) {
    auto gen = std::make_unique<ThreadedTreeGenerator>(ConstructToken{});
    if (!gen->init(workerCount)) {
        return nullptr;
    }
    return gen;
}

ThreadedTreeGenerator::~ThreadedTreeGenerator() {
    if (jobQueue_) {
        jobQueue_->shutdown();
    }
}

bool ThreadedTreeGenerator::init(uint32_t workerCount) {
    jobQueue_ = Loading::LoadJobQueue::create(workerCount);
    if (!jobQueue_) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "ThreadedTreeGenerator: Failed to create job queue");
        return false;
    }
    SDL_Log("ThreadedTreeGenerator initialized with %u workers", workerCount);
    return true;
}

namespace {

// Runs on a worker thread: generate a tree and pack it into CPU-side staged data
std::unique_ptr<Loading::StagedResource> generateStagedTree(const ThreadedTreeGenerator::TreeRequest& request) {
    // Generate tree mesh on worker thread
    TreeGenerator generator;
    TreeMeshData meshData = generator.generate(request.options);

    // Build branch mesh vertices (same algorithm as TreeSystem::generateTreeMesh)
    std::vector<Vertex> branchVertices;
    std::vector<uint32_t> branchIndices;

    glm::vec2 textureScale = request.options.bark.textureScale;
    float vRepeat = 1.0f / textureScale.y;

    uint32_t indexOffset = 0;
    for (const auto& branch : meshData.branches) {
        int sectionCount = branch.sectionCount;
        int segmentCount = branch.segmentCount;

        for (size_t sectionIdx = 0; sectionIdx < branch.sections.size(); ++sectionIdx) {
            const SectionData& section = branch.sections[sectionIdx];
            float vCoord = (sectionIdx % 2 == 0) ? 0.0f : vRepeat;

            for (int seg = 0; seg <= segmentCount; ++seg) {
                float angle = 2.0f * glm::pi<float>() * static_cast<float>(seg) / static_cast<float>(segmentCount);

                glm::vec3 localPos(std::cos(angle), 0.0f, std::sin(angle));
                glm::vec3 localNormal = -localPos;

                glm::vec3 worldOffset = section.orientation * (localPos * section.radius);
                glm::vec3 worldNormal = glm::normalize(section.orientation * localNormal);

                float uCoord = static_cast<float>(seg) / static_cast<float>(segmentCount) * textureScale.x;

                Vertex v{};
                v.position = section.origin + worldOffset;
                v.normal = worldNormal;
                v.texCoord = glm::vec2(uCoord, vCoord);
                v.tangent = glm::vec4(
                    glm::normalize(section.orientation * glm::vec3(0.0f, 1.0f, 0.0f)),
                    1.0f
                );

                float normalizedLevel = static_cast<float>(branch.level) / 3.0f * 0.95f;
                if (branch.level == 0) {
                    v.color = glm::vec4(1.0f, 1.0f, 1.0f, 0.0f);
                } else {
                    v.color = glm::vec4(branch.origin, normalizedLevel);
                }

                branchVertices.push_back(v);
            }
        }

        // Generate indices for this branch
        uint32_t vertsPerRing = static_cast<uint32_t>(segmentCount + 1);
        for (int section = 0; section < sectionCount; ++section) {
            for (int seg = 0; seg < segmentCount; ++seg) {
                uint32_t v0 = indexOffset + section * vertsPerRing + seg;
                uint32_t v1 = v0 + 1;
                uint32_t v2 = v0 + vertsPerRing;
                uint32_t v3 = v2 + 1;

                branchIndices.push_back(v0);
                branchIndices.push_back(v2);
                branchIndices.push_back(v1);

                branchIndices.push_back(v1);
                branchIndices.push_back(v2);
                branchIndices.push_back(v3);
            }
        }

        indexOffset += static_cast<uint32_t>(branch.sections.size()) * vertsPerRing;
    }

    // Build leaf instance data (matching LeafInstanceGPU layout: 32 bytes)
    struct LeafInstanceGPU {
        glm::vec4 positionAndSize;
        glm::vec4 orientation;
    };
    static_assert(sizeof(LeafInstanceGPU) == 32, "LeafInstanceGPU must be 32 bytes");

    std::vector<LeafInstanceGPU> leafInstances;
    for (const auto& leaf : meshData.leaves) {
        int quadsPerLeaf = (request.options.leaves.billboard == BillboardMode::Double) ? 2 : 1;

        for (int quad = 0; quad < quadsPerLeaf; ++quad) {
            float yRotation = (quad == 1) ? glm::half_pi<float>() : 0.0f;
            glm::quat yQuat = glm::angleAxis(yRotation, glm::vec3(0.0f, 1.0f, 0.0f));
            glm::quat finalQuat = leaf.orientation * yQuat;

            LeafInstanceGPU instance;
            instance.positionAndSize = glm::vec4(leaf.position, leaf.size);
            instance.orientation = glm::vec4(finalQuat.x, finalQuat.y, finalQuat.z, finalQuat.w);
            leafInstances.push_back(instance);
        }
    }

    // Create staged tree mesh result
    auto staged = std::make_unique<Loading::StagedTreeMesh>();

    // Copy vertex data as raw bytes
    staged->branchVertexData.resize(branchVertices.size() * sizeof(Vertex));
    std::memcpy(staged->branchVertexData.data(), branchVertices.data(), staged->branchVertexData.size());
    staged->branchVertexCount = static_cast<uint32_t>(branchVertices.size());
    staged->branchVertexStride = sizeof(Vertex);

    // Copy indices
    staged->branchIndices = std::move(branchIndices);

    // Copy leaf instance data as raw bytes
    staged->leafInstanceData.resize(leafInstances.size() * sizeof(LeafInstanceGPU));
    std::memcpy(staged->leafInstanceData.data(), leafInstances.data(), staged->leafInstanceData.size());
    staged->leafInstanceCount = static_cast<uint32_t>(leafInstances.size());

    // Store placement info
    staged->positionX = request.position.x;
    staged->positionY = request.position.y;
    staged->positionZ = request.position.z;
    staged->rotation = request.rotation;
    staged->scale = request.scale;
    staged->archetypeIndex = request.archetypeIndex;
    staged->options = request.options;

    return staged;
}

Loading::LoadJob makeTreeJob(uint32_t id, const ThreadedTreeGenerator::TreeRequest& request) {
    Loading::LoadJob job;
    job.id = "tree_" + std::to_string(id);
    job.phase = "Trees";
    job.priority = 0;
    // Capture request by value for thread safety
    job.execute = [request]() { return generateStagedTree(request); };
    return job;
}

} // namespace

void ThreadedTreeGenerator::setMaxStagedTrees(uint32_t cap) {
    {
        std::lock_guard<std::mutex> lock(requestMutex_);
        maxStaged_ = cap;
    }
    submitHeldRequests();
}

void ThreadedTreeGenerator::queueTree(const TreeRequest& request) {
    queueTrees({request});
}

void ThreadedTreeGenerator::queueTrees(const std::vector<TreeRequest>& requests) {
    pendingCount_ += static_cast<uint32_t>(requests.size());
    totalQueued_ += static_cast<uint32_t>(requests.size());
    jobQueue_->setTotalJobs(totalQueued_.load());

    {
        std::lock_guard<std::mutex> lock(requestMutex_);
        heldRequests_.insert(heldRequests_.end(), requests.begin(), requests.end());
    }
    submitHeldRequests();
}

void ThreadedTreeGenerator::submitHeldRequests() {
    std::vector<Loading::LoadJob> jobs;
    {
        std::lock_guard<std::mutex> lock(requestMutex_);
        while (!heldRequests_.empty() && (maxStaged_ == 0 || inFlight_ < maxStaged_)) {
            jobs.push_back(makeTreeJob(++nextJobId_, heldRequests_.front()));
            heldRequests_.pop_front();
            ++inFlight_;
        }
    }
    if (!jobs.empty()) {
        jobQueue_->submitBatch(std::move(jobs));
    }
}

std::vector<ThreadedTreeGenerator::StagedTree> ThreadedTreeGenerator::getCompletedTrees() {
    auto results = jobQueue_->getCompletedJobs();
    std::vector<StagedTree> trees;
    trees.reserve(results.size());

    {
        std::lock_guard<std::mutex> lock(requestMutex_);
        inFlight_ -= static_cast<uint32_t>(std::min<size_t>(results.size(), inFlight_));
    }

    for (auto& result : results) {
        if (!result.success || !result.resource) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "ThreadedTreeGenerator: Job '%s' failed: %s",
                        result.jobId.c_str(), result.error.c_str());
            pendingCount_--;
            continue;
        }

        // Cast to StagedTreeMesh
        auto* stagedMesh = dynamic_cast<Loading::StagedTreeMesh*>(result.resource.get());
        if (!stagedMesh) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "ThreadedTreeGenerator: Job '%s' returned wrong resource type",
                        result.jobId.c_str());
            pendingCount_--;
            continue;
        }

        StagedTree tree;
        tree.branchVertexData = std::move(stagedMesh->branchVertexData);
        tree.branchIndices = std::move(stagedMesh->branchIndices);
        tree.branchVertexCount = stagedMesh->branchVertexCount;
        tree.leafInstanceData = std::move(stagedMesh->leafInstanceData);
        tree.leafInstanceCount = stagedMesh->leafInstanceCount;
        tree.position = glm::vec3(stagedMesh->positionX, stagedMesh->positionY, stagedMesh->positionZ);
        tree.rotation = stagedMesh->rotation;
        tree.scale = stagedMesh->scale;
        tree.archetypeIndex = stagedMesh->archetypeIndex;
        tree.options = std::move(stagedMesh->options);

        trees.push_back(std::move(tree));
        pendingCount_--;
        completedCount_++;
    }

    // Consumer has taken ownership of these results; let workers refill the window
    submitHeldRequests();

    return trees;
}

bool ThreadedTreeGenerator::isComplete() const {
    {
        std::lock_guard<std::mutex> lock(requestMutex_);
        if (!heldRequests_.empty()) {
            return false;
        }
    }
    return jobQueue_->isComplete();
}

Loading::LoadProgress ThreadedTreeGenerator::getProgress() const {
    return jobQueue_->getProgress();
}

void ThreadedTreeGenerator::waitForAll() {
    // A blocking consumer cannot drain the window, so release the cap first
    setMaxStagedTrees(0);
    jobQueue_->waitForAll();
}
