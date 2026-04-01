#pragma once
#include "CNNEval.hpp"
#include "GameRecord.hpp"
#include <array>
#include <cstdint>

class CNNTrainer {
public:
    static constexpr float BETA1       = 0.9f;
    static constexpr float BETA2       = 0.999f;
    static constexpr float EPSILON     = 1e-8f;
    static constexpr float LR_DEFAULT  = 1e-3f;

    explicit CNNTrainer(CNNEval& model);

    float trainStep(const TrainBatch& batch, float lr = LR_DEFAULT);
    void  resetMoments();
    int   stepCount() const { return t_; }

private:
    CNNEval& model_;
    int      t_ = 0;

    std::array<float, CNNEval::C1_OUT * CNNEval::C1_IN * 9> m_conv1w{}, v_conv1w{};
    std::array<float, CNNEval::C1_OUT>                       m_conv1b{}, v_conv1b{};
    std::array<float, CNNEval::C2_OUT * CNNEval::C1_OUT * 9> m_conv2w{}, v_conv2w{};
    std::array<float, CNNEval::C2_OUT>                       m_conv2b{}, v_conv2b{};
    std::array<float, CNNEval::FC1_OUT * CNNEval::FLAT>      m_fc1w{},   v_fc1w{};
    std::array<float, CNNEval::FC1_OUT>                      m_fc1b{},   v_fc1b{};
    std::array<float, CNNEval::FC1_OUT>                      m_fc2w{},   v_fc2w{};
    float m_fc2b = 0, v_fc2b = 0;

    std::array<float, CNNEval::C1_OUT * CNNEval::C1_IN * 9> g_conv1w{};
    std::array<float, CNNEval::C1_OUT>                       g_conv1b{};
    std::array<float, CNNEval::C2_OUT * CNNEval::C1_OUT * 9> g_conv2w{};
    std::array<float, CNNEval::C2_OUT>                       g_conv2b{};
    std::array<float, CNNEval::FC1_OUT * CNNEval::FLAT>      g_fc1w{};
    std::array<float, CNNEval::FC1_OUT>                      g_fc1b{};
    std::array<float, CNNEval::FC1_OUT>                      g_fc2w{};
    float g_fc2b = 0;

    struct ForwardCache {
        float input  [CNNEval::C1_IN  * 64];
        float map1   [CNNEval::C1_OUT * 64];
        float map1p  [CNNEval::C1_OUT * 64];
        float map2   [CNNEval::C2_OUT * 64];
        float map2p  [CNNEval::C2_OUT * 64];
        float hidden [CNNEval::FC1_OUT];
        float hiddenp[CNNEval::FC1_OUT];
        float raw;
        float output;
    };

    float forwardCache(uint64_t bb0, uint64_t bb1, uint64_t holes,
                       ForwardCache& cache) const;

    void  backwardAccum(const ForwardCache& cache, float target);
    void  adamUpdate(float lr);
};
