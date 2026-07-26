#pragma once

#include <mutex>

/**
 * GraphicsQueueLock - process-wide mutex serializing graphics queue access.
 *
 * vk::Queue is externally synchronized: submits and presents from different
 * threads must not overlap. During async initialization, worker threads
 * submit one-time upload commands (via CommandScope) while the presenting
 * thread submits loading-screen frames, so every submit/present on the
 * graphics queue must hold this lock.
 *
 * There is a single graphics queue in this engine (VulkanContext), so a
 * global mutex is sufficient and avoids threading a mutex reference through
 * every InitInfo struct.
 */
namespace GraphicsQueueLock {

inline std::mutex& mutex() {
    static std::mutex m;
    return m;
}

using Guard = std::lock_guard<std::mutex>;

} // namespace GraphicsQueueLock
