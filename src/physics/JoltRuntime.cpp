#include "JoltRuntime.h"

#include <Jolt/Jolt.h>
#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>

#include <SDL3/SDL_log.h>
#include <cstdarg>
#include <mutex>
#include <unordered_set>

// Callback for traces
static void TraceImpl(const char* inFMT, ...) {
    va_list args;
    va_start(args, inFMT);
    char buffer[1024];
    vsnprintf(buffer, sizeof(buffer), inFMT, args);
    va_end(args);
    SDL_Log("Jolt: %s", buffer);
}

#ifdef JPH_ENABLE_ASSERTS
// Callback for asserts — log once per unique site and continue.
// Jolt debug assertions (e.g., NaN velocity, GJK precision) would
// otherwise call __builtin_trap() and kill the process.
static bool AssertFailedImpl(const char* inExpression, const char* inMessage, const char* inFile, uint32_t inLine) {
    // Rate-limit: only log the first occurrence of each unique (file, line) pair
    static std::mutex logMutex;
    static std::unordered_set<uint64_t> seenAsserts;

    uint64_t key = (static_cast<uint64_t>(inLine) << 32) ^
                   std::hash<const char*>{}(inFile);

    {
        std::lock_guard<std::mutex> lock(logMutex);
        if (!seenAsserts.insert(key).second) {
            return false; // Already logged this assert, skip silently
        }
    }

    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                "Jolt Assert (non-fatal, logged once): %s:%u: (%s) %s",
                inFile, inLine, inExpression, inMessage ? inMessage : "");
    return false; // false = don't trigger JPH_BREAKPOINT, continue execution
}
#endif

namespace {
    std::weak_ptr<JoltRuntime> g_joltRuntime;
    std::mutex g_joltMutex;
}

JoltRuntime::JoltRuntime() {
    // Register allocation hook
    JPH::RegisterDefaultAllocator();

    // Install callbacks
    JPH::Trace = TraceImpl;
    JPH_IF_ENABLE_ASSERTS(JPH::AssertFailed = AssertFailedImpl;)

    // Create factory
    JPH::Factory::sInstance = new JPH::Factory();

    // Register all Jolt physics types
    JPH::RegisterTypes();

    SDL_Log("Jolt runtime initialized");
}

JoltRuntime::~JoltRuntime() {
    // Unregisters all types with the factory and cleans up the default material
    JPH::UnregisterTypes();

    // Destroy factory
    delete JPH::Factory::sInstance;
    JPH::Factory::sInstance = nullptr;

    SDL_Log("Jolt runtime shutdown");
}

std::shared_ptr<JoltRuntime> JoltRuntime::acquire() {
    std::lock_guard<std::mutex> lock(g_joltMutex);
    auto runtime = g_joltRuntime.lock();
    if (!runtime) {
        runtime = std::make_shared<JoltRuntime>();
        g_joltRuntime = runtime;
    }
    return runtime;
}
