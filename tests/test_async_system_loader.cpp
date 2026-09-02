// Tests for Loading::AsyncSystemLoader: dependency ordering, failure
// propagation, and cancellation. The loader is Vulkan-independent; tasks here
// are plain lambdas driven by a small worker pool and a main-thread poll loop.
#include <doctest/doctest.h>

#include "loading/AsyncSystemLoader.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using Loading::AsyncSystemLoader;
using Loading::SystemInitTask;

namespace {

std::unique_ptr<AsyncSystemLoader> makeLoader(uint32_t workers = 2) {
    AsyncSystemLoader::InitInfo info;
    info.workerCount = workers;
    return AsyncSystemLoader::create(info);
}

// Main-thread poll loop as Application/RendererBuilder drive it; bounded so a
// broken loader fails the test instead of hanging it.
void pollUntilComplete(AsyncSystemLoader& loader) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (!loader.isComplete()) {
        loader.pollCompletions(1.0f);
        REQUIRE(std::chrono::steady_clock::now() < deadline);
        std::this_thread::yield();
    }
    loader.pollCompletions();
}

} // namespace

TEST_CASE("AsyncSystemLoader: all tasks succeed") {
    auto loader = makeLoader();
    REQUIRE(loader);

    std::atomic<int> cpuRuns{0};
    std::atomic<int> gpuRuns{0};
    for (const char* id : {"a", "b", "c"}) {
        SystemInitTask task;
        task.id = id;
        task.cpuWork = [&]() { ++cpuRuns; return true; };
        task.gpuWork = [&]() { ++gpuRuns; return true; };
        loader->addTask(std::move(task));
    }

    loader->start();
    pollUntilComplete(*loader);

    CHECK(loader->isComplete());
    CHECK_FALSE(loader->hasError());
    CHECK(cpuRuns == 3);
    CHECK(gpuRuns == 3);
    CHECK(loader->getProgress().completedTasks == 3);
    CHECK(loader->getProgress().progress == doctest::Approx(1.0f));
}

TEST_CASE("AsyncSystemLoader: cpuWork failure reports the task and skips dependents") {
    auto loader = makeLoader();
    REQUIRE(loader);

    std::atomic<bool> dependentCpuRan{false};
    std::atomic<bool> dependentGpuRan{false};
    std::atomic<bool> failedGpuRan{false};

    SystemInitTask failing;
    failing.id = "broken_system";
    failing.cpuWork = []() { return false; };
    failing.gpuWork = [&]() { failedGpuRan = true; return true; };
    loader->addTask(std::move(failing));

    SystemInitTask dependent;
    dependent.id = "dependent";
    dependent.dependencies = {"broken_system"};
    dependent.cpuWork = [&]() { dependentCpuRan = true; return true; };
    dependent.gpuWork = [&]() { dependentGpuRan = true; return true; };
    loader->addTask(std::move(dependent));

    loader->start();
    pollUntilComplete(*loader);

    CHECK(loader->isComplete());
    CHECK(loader->hasError());
    CHECK(loader->getErrorMessage().find("broken_system") != std::string::npos);
    CHECK_FALSE(failedGpuRan);
    CHECK_FALSE(dependentCpuRan);
    CHECK_FALSE(dependentGpuRan);

    // Cancelling after a failure joins the remaining workers cleanly.
    loader->cancel();
    CHECK_FALSE(dependentCpuRan);
}

TEST_CASE("AsyncSystemLoader: a dependent's cpuWork observes its dependency's gpuWork") {
    auto loader = makeLoader(3);
    REQUIRE(loader);

    // The dependency's cpuWork lingers so a naive scheduler that only waited
    // for cpuWork (or for nothing) would start the dependent too early.
    std::atomic<bool> depCpuDone{false};
    std::atomic<bool> depGpuDone{false};
    std::atomic<bool> dependentSawGpu{false};
    std::atomic<bool> dependentSawCpu{false};

    SystemInitTask dep;
    dep.id = "dep";
    dep.cpuWork = [&]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        depCpuDone = true;
        return true;
    };
    dep.gpuWork = [&]() { depGpuDone = true; return true; };
    loader->addTask(std::move(dep));

    // Unrelated sibling keeps a worker busy so scheduling is exercised.
    SystemInitTask sibling;
    sibling.id = "sibling";
    sibling.cpuWork = []() { return true; };
    loader->addTask(std::move(sibling));

    SystemInitTask dependent;
    dependent.id = "dependent";
    dependent.dependencies = {"dep"};
    dependent.cpuWork = [&]() {
        dependentSawCpu = depCpuDone.load();
        dependentSawGpu = depGpuDone.load();
        return true;
    };
    loader->addTask(std::move(dependent));

    loader->start();
    pollUntilComplete(*loader);

    CHECK_FALSE(loader->hasError());
    CHECK(dependentSawCpu);
    CHECK(dependentSawGpu);
}

TEST_CASE("AsyncSystemLoader: cancel while tasks are pending joins cleanly") {
    auto loader = makeLoader(2);
    REQUIRE(loader);

    std::mutex gateMutex;
    std::condition_variable gateCv;
    bool gateOpen = false;
    std::atomic<int> started{0};
    std::atomic<bool> bothBlocked{false};
    std::atomic<int> ranAfterBlock{0};

    // Two blocking tasks occupy both workers. Scheduling order among
    // dependency-free tasks is unspecified, so some queued tasks may run
    // before both blockers start; none may start once both workers are held
    // and cancel() has been issued.
    for (const char* id : {"block_a", "block_b"}) {
        SystemInitTask task;
        task.id = id;
        task.cpuWork = [&]() {
            ++started;
            std::unique_lock<std::mutex> lock(gateMutex);
            gateCv.wait(lock, [&] { return gateOpen; });
            return true;
        };
        loader->addTask(std::move(task));
    }
    for (const char* id : {"queued_a", "queued_b", "queued_c"}) {
        SystemInitTask task;
        task.id = id;
        task.cpuWork = [&]() {
            if (bothBlocked) ++ranAfterBlock;
            return true;
        };
        loader->addTask(std::move(task));
    }

    loader->start();
    while (started < 2) std::this_thread::yield();
    bothBlocked = true;  // both workers are now inside the blockers

    std::thread canceller([&] { loader->cancel(); });
    // Give cancel() a moment to flag running_=false before releasing the workers.
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    {
        std::lock_guard<std::mutex> lock(gateMutex);
        gateOpen = true;
    }
    gateCv.notify_all();
    canceller.join();

    CHECK(ranAfterBlock == 0);
    CHECK_FALSE(loader->hasError());

    // Idempotent: a second cancel and the destructor's shutdown are no-ops.
    loader->cancel();
    loader->shutdown();
    loader.reset();
}

TEST_CASE("AsyncSystemLoader: cancel before start is safe") {
    auto loader = makeLoader(1);
    REQUIRE(loader);
    SystemInitTask task;
    task.id = "never_started";
    task.cpuWork = []() { return true; };
    loader->addTask(std::move(task));
    loader->cancel();
    loader->cancel();
    CHECK_FALSE(loader->isComplete());
    CHECK_FALSE(loader->hasError());
}
