#pragma once
#include <cstdint>
#include <string>
#include <array>
#include <cmath>

// Small CNN on 8x8x3 input for Blob War board evaluation.
// Architecture:
//   Conv1: 3ch -> 16 filters, 3x3, same padding, ReLU
//   Conv2: 16ch -> 32 filters, 3x3, same padding, ReLU
//   Flatten: 32*8*8 = 2048
//   FC1: 2048 -> 64, ReLU
//   FC2: 64 -> 1, tanh scaled to [-64, +64]
class CNNEval {
public:
    static constexpr int W = 8;
    static constexpr int H = 8;
    static constexpr int C1_IN  = 3;
    static constexpr int C1_OUT = 16;
    static constexpr int C2_OUT = 32;
    static constexpr int FLAT   = C2_OUT * W * H; // 2048
    static constexpr int FC1_OUT = 64;
    static constexpr float SCORE_SCALE = 64.0f;

    CNNEval();

    // Main evaluation entry point. Returns score from P1's perspective.
    float evaluate(uint64_t bb0, uint64_t bb1, uint64_t holes) const;

    // Weight I/O. Returns true on success.
    bool loadWeights(const std::string& path);
    bool saveWeights(const std::string& path) const;

    // Re-initialize with random small weights (for training).
    void randomInit(unsigned seed = 42);

private:
    friend class CNNTrainer;

    // Conv1 weights: [C1_OUT][C1_IN][3][3]
    std::array<float, C1_OUT * C1_IN * 9> conv1w;
    std::array<float, C1_OUT>             conv1b;

    // Conv2 weights: [C2_OUT][C1_OUT][3][3]
    std::array<float, C2_OUT * C1_OUT * 9> conv2w;
    std::array<float, C2_OUT>              conv2b;

    // FC1 weights: [FC1_OUT][FLAT]
    std::array<float, FC1_OUT * FLAT> fc1w;
    std::array<float, FC1_OUT>        fc1b;

    // FC2 weights: [1][FC1_OUT]
    std::array<float, FC1_OUT> fc2w;
    float                      fc2b;

    void conv2d(const float* in, int ch_in, float* out, int ch_out,
                const float* w, const float* b) const;
    void fc(const float* in, int in_sz, float* out, int out_sz,
            const float* w, const float* b) const;
    static inline float relu(float x) { return x > 0.0f ? x : 0.0f; }
};
