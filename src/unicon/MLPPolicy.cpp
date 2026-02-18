#include "MLPPolicy.h"

#include <SDL3/SDL_log.h>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <random>

// ELU activation: f(x) = x if x > 0, else alpha * (exp(x) - 1)
static void applyELU(float* data, size_t count, float alpha = 1.0f) {
    for (size_t i = 0; i < count; ++i) {
        if (data[i] < 0.0f) {
            data[i] = alpha * (std::exp(data[i]) - 1.0f);
        }
    }
}

// Matrix-vector multiply: out = weights * in + biases
// weights is [outDim x inDim] row-major
static void matVecMulAdd(const float* weights, const float* biases,
                         const float* in, float* out,
                         size_t outDim, size_t inDim) {
    for (size_t row = 0; row < outDim; ++row) {
        float sum = biases[row];
        const float* rowPtr = weights + row * inDim;
        for (size_t col = 0; col < inDim; ++col) {
            sum += rowPtr[col] * in[col];
        }
        out[row] = sum;
    }
}

bool MLPPolicy::loadWeights(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "MLPPolicy::loadWeights: cannot open '%s'", path.c_str());
        return false;
    }

    uint32_t magic = 0;
    uint32_t numLayers = 0;
    file.read(reinterpret_cast<char*>(&magic), sizeof(magic));
    file.read(reinterpret_cast<char*>(&numLayers), sizeof(numLayers));

    if (magic != MAGIC) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "MLPPolicy::loadWeights: bad magic 0x%08X (expected 0x%08X)",
                     magic, MAGIC);
        return false;
    }

    if (numLayers == 0 || numLayers > 100) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "MLPPolicy::loadWeights: invalid layer count %u", numLayers);
        return false;
    }

    layers_.resize(numLayers);

    for (uint32_t i = 0; i < numLayers; ++i) {
        uint32_t inDim = 0, outDim = 0;
        file.read(reinterpret_cast<char*>(&inDim), sizeof(inDim));
        file.read(reinterpret_cast<char*>(&outDim), sizeof(outDim));

        if (!file || inDim == 0 || outDim == 0 || inDim > 100000 || outDim > 100000) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "MLPPolicy::loadWeights: invalid layer %u dims (%u x %u)",
                         i, inDim, outDim);
            layers_.clear();
            return false;
        }

        layers_[i].inputDim = inDim;
        layers_[i].outputDim = outDim;

        size_t weightCount = static_cast<size_t>(outDim) * inDim;
        layers_[i].weights.resize(weightCount);
        layers_[i].biases.resize(outDim);

        file.read(reinterpret_cast<char*>(layers_[i].weights.data()),
                  weightCount * sizeof(float));
        file.read(reinterpret_cast<char*>(layers_[i].biases.data()),
                  outDim * sizeof(float));

        if (!file) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "MLPPolicy::loadWeights: unexpected EOF at layer %u", i);
            layers_.clear();
            return false;
        }
    }

    // Allocate scratch buffers for the largest layer dimensions
    size_t maxDim = 0;
    for (const auto& layer : layers_) {
        maxDim = std::max(maxDim, std::max(layer.inputDim, layer.outputDim));
    }
    buffer0_.resize(maxDim, 0.0f);
    buffer1_.resize(maxDim, 0.0f);

    SDL_Log("MLPPolicy loaded: %zu layers from '%s' (input=%zu, output=%zu)",
            layers_.size(), path.c_str(), getInputDim(), getOutputDim());

    return true;
}

void MLPPolicy::evaluate(const std::vector<float>& observation,
                          std::vector<float>& action) const {
    if (layers_.empty()) {
        action.clear();
        return;
    }

    const size_t inDim = getInputDim();
    if (observation.size() != inDim) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "MLPPolicy::evaluate: observation size %zu != expected %zu",
                     observation.size(), inDim);
        action.assign(getOutputDim(), 0.0f);
        return;
    }

    // Ping-pong between buffer0_ and buffer1_ for intermediate results.
    // Input goes into buffer0_ first.
    const float* input = observation.data();

    for (size_t i = 0; i < layers_.size(); ++i) {
        const auto& layer = layers_[i];
        float* output = (i % 2 == 0) ? buffer0_.data() : buffer1_.data();

        matVecMulAdd(layer.weights.data(), layer.biases.data(),
                     input, output, layer.outputDim, layer.inputDim);

        // Apply ELU to all hidden layers, but NOT the output layer
        if (i + 1 < layers_.size()) {
            applyELU(output, layer.outputDim);
        }

        input = output;
    }

    // Copy final output
    const size_t outDim = getOutputDim();
    action.resize(outDim);
    const float* finalOutput = ((layers_.size() - 1) % 2 == 0) ? buffer0_.data() : buffer1_.data();
    std::copy(finalOutput, finalOutput + outDim, action.data());
}

size_t MLPPolicy::getInputDim() const {
    return layers_.empty() ? 0 : layers_.front().inputDim;
}

size_t MLPPolicy::getOutputDim() const {
    return layers_.empty() ? 0 : layers_.back().outputDim;
}

void MLPPolicy::initRandom(size_t inputDim, size_t outputDim,
                            size_t hiddenDim, size_t hiddenLayers) {
    layers_.clear();

    std::mt19937 rng(42); // Fixed seed for reproducibility

    auto makeLayer = [&](size_t inDim, size_t outDim) {
        MLPLayer layer;
        layer.inputDim = inDim;
        layer.outputDim = outDim;

        // Xavier initialization: stddev = sqrt(2 / (in + out))
        float stddev = std::sqrt(2.0f / static_cast<float>(inDim + outDim));
        std::normal_distribution<float> dist(0.0f, stddev);

        layer.weights.resize(outDim * inDim);
        for (auto& w : layer.weights) w = dist(rng);

        layer.biases.assign(outDim, 0.0f);

        return layer;
    };

    // Input -> first hidden
    layers_.push_back(makeLayer(inputDim, hiddenDim));

    // Hidden -> hidden
    for (size_t i = 1; i < hiddenLayers; ++i) {
        layers_.push_back(makeLayer(hiddenDim, hiddenDim));
    }

    // Last hidden -> output
    layers_.push_back(makeLayer(hiddenDim, outputDim));

    // Allocate scratch buffers
    size_t maxDim = std::max({inputDim, outputDim, hiddenDim});
    buffer0_.resize(maxDim, 0.0f);
    buffer1_.resize(maxDim, 0.0f);

    SDL_Log("MLPPolicy initRandom: %zu layers (input=%zu, hidden=%zu x %zu, output=%zu)",
            layers_.size(), inputDim, hiddenDim, hiddenLayers, outputDim);
}
