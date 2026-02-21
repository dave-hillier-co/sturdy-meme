#pragma once

#include <string>
#include <vector>
#include <cstdint>

// A single fully-connected layer: output = activation(weights * input + biases)
struct MLPLayer {
    std::vector<float> weights;  // [outputDim * inputDim], row-major
    std::vector<float> biases;   // [outputDim]
    size_t inputDim = 0;
    size_t outputDim = 0;
};

// Lightweight MLP inference for the UniCon low-level policy.
//
// Network architecture (from the paper): 3 hidden layers of 1024 units each.
// Hidden layers use ELU activation; output layer is linear (raw torques).
//
// Weight file format (little-endian):
//   Header:
//     uint32_t magic = 0x4D4C5001  ("MLP\x01")
//     uint32_t numLayers
//   Per layer:
//     uint32_t inputDim
//     uint32_t outputDim
//     float[outputDim * inputDim] weights (row-major)
//     float[outputDim] biases
class MLPPolicy {
public:
    // Load weights from binary file.
    bool loadWeights(const std::string& path);

    // Forward pass: observation -> action (torques).
    // observation.size() must equal getInputDim().
    // action is resized to getOutputDim().
    void evaluate(const std::vector<float>& observation,
                  std::vector<float>& action) const;

    size_t getInputDim() const;
    size_t getOutputDim() const;
    size_t getLayerCount() const { return layers_.size(); }
    bool isLoaded() const { return !layers_.empty(); }

    // Build a random policy for testing (outputs random-ish torques).
    // Weights are initialized with small random values so the policy
    // produces non-zero but bounded outputs.
    void initRandom(size_t inputDim, size_t outputDim,
                    size_t hiddenDim = 1024, size_t hiddenLayers = 3);

    static constexpr uint32_t MAGIC = 0x4D4C5001; // "MLP\x01"

private:
    std::vector<MLPLayer> layers_;

    // Scratch buffers for intermediate activations (mutable for const evaluate)
    mutable std::vector<float> buffer0_;
    mutable std::vector<float> buffer1_;
};
