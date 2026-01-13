#pragma once

#include <cstdint>
#include <functional>

/**
 * Type-safe handle wrappers for different asset types.
 *
 * Handles are lightweight identifiers that reference assets stored in the AssetRegistry.
 * They use strong typing to prevent accidentally mixing different asset types.
 *
 * Design principles:
 * - Handles are cheap to copy (just a uint32_t)
 * - Invalid handles have index == INVALID_INDEX
 * - Handles don't own the resource - AssetRegistry manages lifecycle
 */

namespace asset {

// Base handle with generation for ABA problem detection
template<typename Tag>
struct Handle {
    static constexpr uint32_t INVALID_INDEX = ~0u;

    uint32_t index = INVALID_INDEX;
    uint32_t generation = 0;

    Handle() = default;
    explicit Handle(uint32_t idx, uint32_t gen = 0) : index(idx), generation(gen) {}

    bool isValid() const { return index != INVALID_INDEX; }
    explicit operator bool() const { return isValid(); }

    bool operator==(const Handle& other) const {
        return index == other.index && generation == other.generation;
    }
    bool operator!=(const Handle& other) const {
        return !(*this == other);
    }

    // For use in unordered containers
    struct Hash {
        size_t operator()(const Handle& h) const {
            return std::hash<uint64_t>{}(
                (static_cast<uint64_t>(h.generation) << 32) | h.index
            );
        }
    };
};

// Tag types for type safety
struct TextureTag {};
struct MeshTag {};
struct ShaderTag {};

// Concrete handle types
using TextureHandle = Handle<TextureTag>;
using MeshHandle = Handle<MeshTag>;
using ShaderHandle = Handle<ShaderTag>;

// Invalid handle constants
inline constexpr TextureHandle INVALID_TEXTURE_HANDLE{};
inline constexpr MeshHandle INVALID_MESH_HANDLE{};
inline constexpr ShaderHandle INVALID_SHADER_HANDLE{};

} // namespace asset
