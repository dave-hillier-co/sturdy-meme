#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

/**
 * WeakCache - Thread-safe cache using weak_ptr for automatic cleanup
 *
 * Stores weak references to shared_ptr-managed objects. When all external
 * references to an object are released, the object is automatically destroyed
 * and the cache entry becomes stale (returns nullptr on lookup).
 *
 * Usage:
 *   WeakCache<Texture> cache;
 *   cache.put("brick", texturePtr);
 *   auto tex = cache.get("brick");  // Returns shared_ptr or nullptr if expired
 */
template<typename T>
class WeakCache {
public:
    /**
     * Get an item from the cache.
     * Returns nullptr if not found or if the weak_ptr has expired.
     * Automatically removes stale entries on access.
     */
    std::shared_ptr<T> get(const std::string& key) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = cache_.find(key);
        if (it != cache_.end()) {
            if (auto ptr = it->second.lock()) {
                hits_++;
                return ptr;
            }
            // Expired - remove stale entry
            cache_.erase(it);
        }
        return nullptr;
    }

    /**
     * Store an item in the cache.
     * Overwrites any existing entry with the same key.
     */
    void put(const std::string& key, std::shared_ptr<T> value) {
        if (!value) return;
        std::lock_guard<std::mutex> lock(mutex_);
        cache_[key] = value;
    }

    /**
     * Remove an item from the cache.
     */
    void remove(const std::string& key) {
        std::lock_guard<std::mutex> lock(mutex_);
        cache_.erase(key);
    }

    /**
     * Check if key exists and is not expired.
     */
    bool contains(const std::string& key) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = cache_.find(key);
        if (it != cache_.end()) {
            if (!it->second.expired()) {
                return true;
            }
            cache_.erase(it);
        }
        return false;
    }

    /**
     * Get count of non-expired entries.
     */
    size_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        size_t count = 0;
        for (const auto& [key, wp] : cache_) {
            if (!wp.expired()) count++;
        }
        return count;
    }

    /**
     * Get cache hit count.
     */
    size_t hits() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return hits_;
    }

    /**
     * Remove all expired entries from the cache.
     * Returns number of entries removed.
     */
    size_t prune() {
        std::lock_guard<std::mutex> lock(mutex_);
        size_t removed = 0;
        for (auto it = cache_.begin(); it != cache_.end();) {
            if (it->second.expired()) {
                it = cache_.erase(it);
                removed++;
            } else {
                ++it;
            }
        }
        return removed;
    }

    /**
     * Clear all entries from the cache.
     */
    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        cache_.clear();
    }

private:
    std::unordered_map<std::string, std::weak_ptr<T>> cache_;
    mutable size_t hits_ = 0;
    mutable std::mutex mutex_;
};
