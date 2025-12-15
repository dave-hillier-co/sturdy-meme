#pragma once

#include <functional>
#include <optional>
#include <utility>

// Adapts classes with init/destroy pattern to RAII semantics.
// Does not modify the underlying class - only changes callsites.
//
// Usage:
//   auto pipelines = RAIIAdapter<TerrainPipelines>::create(
//       [&](auto& p) { return p.init(info); },
//       [device](auto& p) { p.destroy(device); }
//   );
//   if (!pipelines) return false;
//   pipelines->getRenderPipeline();  // Access via ->

template<typename T>
class RAIIAdapter {
public:
    template<typename InitFunc, typename DestroyFunc>
    static std::optional<RAIIAdapter> create(InitFunc&& init, DestroyFunc&& destroy) {
        RAIIAdapter adapter(std::forward<DestroyFunc>(destroy));
        if (!init(adapter.value_)) {
            return std::nullopt;
        }
        return adapter;
    }

    ~RAIIAdapter() {
        if (destroy_) destroy_(value_);
    }

    // Move-only
    RAIIAdapter(RAIIAdapter&& other) noexcept
        : value_(std::move(other.value_))
        , destroy_(std::move(other.destroy_)) {
        other.destroy_ = nullptr;
    }

    RAIIAdapter& operator=(RAIIAdapter&& other) noexcept {
        if (this != &other) {
            if (destroy_) destroy_(value_);
            value_ = std::move(other.value_);
            destroy_ = std::move(other.destroy_);
            other.destroy_ = nullptr;
        }
        return *this;
    }

    RAIIAdapter(const RAIIAdapter&) = delete;
    RAIIAdapter& operator=(const RAIIAdapter&) = delete;

    T& get() { return value_; }
    const T& get() const { return value_; }
    T* operator->() { return &value_; }
    const T* operator->() const { return &value_; }
    T& operator*() { return value_; }
    const T& operator*() const { return value_; }

private:
    template<typename DestroyFunc>
    explicit RAIIAdapter(DestroyFunc&& destroy)
        : destroy_(std::forward<DestroyFunc>(destroy)) {}

    T value_{};
    std::function<void(T&)> destroy_;
};
