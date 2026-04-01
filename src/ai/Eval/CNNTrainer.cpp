#include "CNNTrainer.hpp"
#include <cmath>
#include <cstring>

CNNTrainer::CNNTrainer(CNNEval& model) : model_(model) {}

void CNNTrainer::resetMoments() {
    m_conv1w.fill(0); v_conv1w.fill(0);
    m_conv1b.fill(0); v_conv1b.fill(0);
    m_conv2w.fill(0); v_conv2w.fill(0);
    m_conv2b.fill(0); v_conv2b.fill(0);
    m_fc1w.fill(0);   v_fc1w.fill(0);
    m_fc1b.fill(0);   v_fc1b.fill(0);
    m_fc2w.fill(0);   v_fc2w.fill(0);
    m_fc2b = 0; v_fc2b = 0;
    t_ = 0;
}

// ---------------------------------------------------------------------------
// forwardCache
// ---------------------------------------------------------------------------
float CNNTrainer::forwardCache(uint64_t bb0, uint64_t bb1, uint64_t holes,
                               ForwardCache& cache) const {
    constexpr int W = CNNEval::W;
    constexpr int H = CNNEval::H;

    std::memset(cache.input, 0, sizeof(cache.input));
    for (int sq = 0; sq < 64; ++sq) {
        if ((bb0   >> sq) & 1) cache.input[0 * 64 + sq] = 1.0f;
        if ((bb1   >> sq) & 1) cache.input[1 * 64 + sq] = 1.0f;
        if ((holes >> sq) & 1) cache.input[2 * 64 + sq] = 1.0f;
    }

    // Conv1: [C1_IN][8][8] -> [C1_OUT][8][8], save pre- and post-ReLU
    for (int oc = 0; oc < CNNEval::C1_OUT; ++oc) {
        for (int y = 0; y < H; ++y) {
            for (int x = 0; x < W; ++x) {
                float sum = model_.conv1b[oc];
                for (int ic = 0; ic < CNNEval::C1_IN; ++ic) {
                    for (int ky = -1; ky <= 1; ++ky) {
                        int sy = y + ky;
                        if (sy < 0 || sy >= H) continue;
                        for (int kx = -1; kx <= 1; ++kx) {
                            int sx = x + kx;
                            if (sx < 0 || sx >= W) continue;
                            int in_idx = ic * 64 + sy * W + sx;
                            int w_idx  = ((oc * CNNEval::C1_IN + ic) * 3 + (ky + 1)) * 3 + (kx + 1);
                            sum += cache.input[in_idx] * model_.conv1w[w_idx];
                        }
                    }
                }
                int out_idx = oc * 64 + y * W + x;
                cache.map1p[out_idx] = sum;
                cache.map1 [out_idx] = sum > 0.0f ? sum : 0.0f;
            }
        }
    }

    // Conv2: [C1_OUT][8][8] -> [C2_OUT][8][8]
    for (int oc = 0; oc < CNNEval::C2_OUT; ++oc) {
        for (int y = 0; y < H; ++y) {
            for (int x = 0; x < W; ++x) {
                float sum = model_.conv2b[oc];
                for (int ic = 0; ic < CNNEval::C1_OUT; ++ic) {
                    for (int ky = -1; ky <= 1; ++ky) {
                        int sy = y + ky;
                        if (sy < 0 || sy >= H) continue;
                        for (int kx = -1; kx <= 1; ++kx) {
                            int sx = x + kx;
                            if (sx < 0 || sx >= W) continue;
                            int in_idx = ic * 64 + sy * W + sx;
                            int w_idx  = ((oc * CNNEval::C1_OUT + ic) * 3 + (ky + 1)) * 3 + (kx + 1);
                            sum += cache.map1[in_idx] * model_.conv2w[w_idx];
                        }
                    }
                }
                int out_idx = oc * 64 + y * W + x;
                cache.map2p[out_idx] = sum;
                cache.map2 [out_idx] = sum > 0.0f ? sum : 0.0f;
            }
        }
    }

    // FC1: FLAT -> FC1_OUT, ReLU
    for (int i = 0; i < CNNEval::FC1_OUT; ++i) {
        float sum = model_.fc1b[i];
        const float* row = model_.fc1w.data() + i * CNNEval::FLAT;
        for (int j = 0; j < CNNEval::FLAT; ++j)
            sum += row[j] * cache.map2[j];
        cache.hiddenp[i] = sum;
        cache.hidden [i] = sum > 0.0f ? sum : 0.0f;
    }

    // FC2: FC1_OUT -> 1
    float raw = model_.fc2b;
    for (int j = 0; j < CNNEval::FC1_OUT; ++j)
        raw += model_.fc2w[j] * cache.hidden[j];
    cache.raw    = raw;
    cache.output = std::tanh(raw) * CNNEval::SCORE_SCALE;
    return cache.output;
}

// ---------------------------------------------------------------------------
// backwardAccum
// ---------------------------------------------------------------------------
void CNNTrainer::backwardAccum(const ForwardCache& cache, float target) {
    constexpr int W    = CNNEval::W;
    constexpr int H    = CNNEval::H;
    constexpr int FLAT = CNNEval::FLAT;

    // dL/d_output (MSE)
    float d_output = 2.0f * (cache.output - target);

    // tanh*64 backward
    float tanh_raw = std::tanh(cache.raw);
    float d_raw = d_output * (CNNEval::SCORE_SCALE * (1.0f - tanh_raw * tanh_raw));

    // FC2 backward
    g_fc2b += d_raw;
    float d_hidden[CNNEval::FC1_OUT];
    for (int j = 0; j < CNNEval::FC1_OUT; ++j) {
        g_fc2w[j] += d_raw * cache.hidden[j];
        d_hidden[j] = d_raw * model_.fc2w[j];
    }

    // ReLU hidden
    for (int j = 0; j < CNNEval::FC1_OUT; ++j)
        d_hidden[j] *= (cache.hiddenp[j] > 0.0f) ? 1.0f : 0.0f;

    // FC1 backward
    float d_map2[FLAT] = {};
    for (int i = 0; i < CNNEval::FC1_OUT; ++i) {
        g_fc1b[i] += d_hidden[i];
        const float* row = model_.fc1w.data() + i * FLAT;
        for (int j = 0; j < FLAT; ++j) {
            g_fc1w[i * FLAT + j] += d_hidden[i] * cache.map2[j];
            d_map2[j] += row[j] * d_hidden[i];
        }
    }

    // ReLU map2
    for (int k = 0; k < CNNEval::C2_OUT * 64; ++k)
        d_map2[k] *= (cache.map2p[k] > 0.0f) ? 1.0f : 0.0f;

    // Conv2 backward
    float d_map1[CNNEval::C1_OUT * 64] = {};
    for (int oc = 0; oc < CNNEval::C2_OUT; ++oc) {
        for (int y = 0; y < H; ++y) {
            for (int x = 0; x < W; ++x) {
                float d = d_map2[oc * 64 + y * W + x];
                g_conv2b[oc] += d;
                for (int ic = 0; ic < CNNEval::C1_OUT; ++ic) {
                    for (int ky = -1; ky <= 1; ++ky) {
                        int sy = y + ky;
                        if (sy < 0 || sy >= H) continue;
                        for (int kx = -1; kx <= 1; ++kx) {
                            int sx = x + kx;
                            if (sx < 0 || sx >= W) continue;
                            int w_idx  = ((oc * CNNEval::C1_OUT + ic) * 3 + (ky + 1)) * 3 + (kx + 1);
                            int in_idx = ic * 64 + sy * W + sx;
                            g_conv2w[w_idx] += d * cache.map1[in_idx];
                            d_map1[in_idx]  += model_.conv2w[w_idx] * d;
                        }
                    }
                }
            }
        }
    }

    // ReLU map1
    for (int k = 0; k < CNNEval::C1_OUT * 64; ++k)
        d_map1[k] *= (cache.map1p[k] > 0.0f) ? 1.0f : 0.0f;

    // Conv1 backward
    for (int oc = 0; oc < CNNEval::C1_OUT; ++oc) {
        for (int y = 0; y < H; ++y) {
            for (int x = 0; x < W; ++x) {
                float d = d_map1[oc * 64 + y * W + x];
                g_conv1b[oc] += d;
                for (int ic = 0; ic < CNNEval::C1_IN; ++ic) {
                    for (int ky = -1; ky <= 1; ++ky) {
                        int sy = y + ky;
                        if (sy < 0 || sy >= H) continue;
                        for (int kx = -1; kx <= 1; ++kx) {
                            int sx = x + kx;
                            if (sx < 0 || sx >= W) continue;
                            int w_idx  = ((oc * CNNEval::C1_IN + ic) * 3 + (ky + 1)) * 3 + (kx + 1);
                            int in_idx = ic * 64 + sy * W + sx;
                            g_conv1w[w_idx] += d * cache.input[in_idx];
                        }
                    }
                }
            }
        }
    }
}

// ---------------------------------------------------------------------------
// adamUpdate
// ---------------------------------------------------------------------------
void CNNTrainer::adamUpdate(float lr) {
    ++t_;
    float bc1 = 1.0f - std::pow(BETA1, (float)t_);
    float bc2 = 1.0f - std::pow(BETA2, (float)t_);

    auto step = [&](float* theta, float* m, float* v, float* g, int n) {
        for (int i = 0; i < n; ++i) {
            m[i] = BETA1 * m[i] + (1.0f - BETA1) * g[i];
            v[i] = BETA2 * v[i] + (1.0f - BETA2) * g[i] * g[i];
            float mh = m[i] / bc1;
            float vh = v[i] / bc2;
            theta[i] -= lr * mh / (std::sqrt(vh) + EPSILON);
            g[i] = 0.0f;
        }
    };

    step(model_.conv1w.data(), m_conv1w.data(), v_conv1w.data(), g_conv1w.data(), (int)g_conv1w.size());
    step(model_.conv1b.data(), m_conv1b.data(), v_conv1b.data(), g_conv1b.data(), (int)g_conv1b.size());
    step(model_.conv2w.data(), m_conv2w.data(), v_conv2w.data(), g_conv2w.data(), (int)g_conv2w.size());
    step(model_.conv2b.data(), m_conv2b.data(), v_conv2b.data(), g_conv2b.data(), (int)g_conv2b.size());
    step(model_.fc1w.data(),   m_fc1w.data(),   v_fc1w.data(),   g_fc1w.data(),   (int)g_fc1w.size());
    step(model_.fc1b.data(),   m_fc1b.data(),   v_fc1b.data(),   g_fc1b.data(),   (int)g_fc1b.size());
    step(model_.fc2w.data(),   m_fc2w.data(),   v_fc2w.data(),   g_fc2w.data(),   (int)g_fc2w.size());

    // FC2 bias (scalar)
    m_fc2b = BETA1 * m_fc2b + (1.0f - BETA1) * g_fc2b;
    v_fc2b = BETA2 * v_fc2b + (1.0f - BETA2) * g_fc2b * g_fc2b;
    model_.fc2b -= lr * (m_fc2b / bc1) / (std::sqrt(v_fc2b / bc2) + EPSILON);
    g_fc2b = 0.0f;
}

// ---------------------------------------------------------------------------
// trainStep
// ---------------------------------------------------------------------------
float CNNTrainer::trainStep(const TrainBatch& batch, float lr) {
    g_conv1w.fill(0); g_conv1b.fill(0);
    g_conv2w.fill(0); g_conv2b.fill(0);
    g_fc1w.fill(0);   g_fc1b.fill(0);
    g_fc2w.fill(0);   g_fc2b = 0.0f;

    float total_loss = 0.0f;
    for (const auto& s : batch) {
        ForwardCache cache;
        float out  = forwardCache(s.bb0, s.bb1, s.holes, cache);
        float diff = out - s.target;
        total_loss += diff * diff;
        backwardAccum(cache, s.target);
    }

    float inv = 1.0f / (float)batch.size();
    for (auto& g : g_conv1w) g *= inv;
    for (auto& g : g_conv1b) g *= inv;
    for (auto& g : g_conv2w) g *= inv;
    for (auto& g : g_conv2b) g *= inv;
    for (auto& g : g_fc1w)   g *= inv;
    for (auto& g : g_fc1b)   g *= inv;
    for (auto& g : g_fc2w)   g *= inv;
    g_fc2b *= inv;

    adamUpdate(lr);
    return total_loss * inv;
}
