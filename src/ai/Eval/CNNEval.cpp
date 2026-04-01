#include "CNNEval.hpp"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cmath>
#include <random>

// ---------------------------------------------------------------------------
// Constructor: try to load default weights, otherwise random init.
// ---------------------------------------------------------------------------
CNNEval::CNNEval() {
    randomInit();
    if (!loadWeights("cnn_weights.bin")) {
        std::fprintf(stderr, "[CNNEval] cnn_weights.bin not found — using random weights\n");
    }
}

// ---------------------------------------------------------------------------
// randomInit
// ---------------------------------------------------------------------------
void CNNEval::randomInit(unsigned seed) {
    std::mt19937 rng(seed);

    auto he = [&](float fan_in) -> float {
        std::normal_distribution<float> dist(0.0f, std::sqrt(2.0f / fan_in));
        return dist(rng);
    };

    // Conv1: fan_in = C1_IN * 3 * 3 = 27
    for (auto& v : conv1w) v = he(27.0f);
    for (auto& v : conv1b) v = 0.0f;

    // Conv2: fan_in = C1_OUT * 3 * 3 = 144
    for (auto& v : conv2w) v = he(144.0f);
    for (auto& v : conv2b) v = 0.0f;

    // FC1: fan_in = FLAT = 2048
    for (auto& v : fc1w) v = he((float)FLAT);
    for (auto& v : fc1b) v = 0.0f;

    // FC2: fan_in = FC1_OUT = 64
    for (auto& v : fc2w) v = he((float)FC1_OUT);
    fc2b = 0.0f;
}

// ---------------------------------------------------------------------------
// conv2d: same-padding 3x3, ch_in channels -> ch_out channels.
//   w layout: [ch_out][ch_in][3][3]  (row-major)
// ---------------------------------------------------------------------------
void CNNEval::conv2d(const float* in, int ch_in,
                     float* out, int ch_out,
                     const float* w, const float* b) const {
    for (int oc = 0; oc < ch_out; ++oc) {
        for (int y = 0; y < H; ++y) {
            for (int x = 0; x < W; ++x) {
                float sum = b[oc];
                for (int ic = 0; ic < ch_in; ++ic) {
                    for (int ky = -1; ky <= 1; ++ky) {
                        int sy = y + ky;
                        if (sy < 0 || sy >= H) continue;
                        for (int kx = -1; kx <= 1; ++kx) {
                            int sx = x + kx;
                            if (sx < 0 || sx >= W) continue;
                            int in_idx  = ic * H * W + sy * W + sx;
                            int w_idx   = ((oc * ch_in + ic) * 3 + (ky + 1)) * 3 + (kx + 1);
                            sum += in[in_idx] * w[w_idx];
                        }
                    }
                }
                out[oc * H * W + y * W + x] = relu(sum);
            }
        }
    }
}

// ---------------------------------------------------------------------------
// fc: fully connected layer.
//   w layout: [out_sz][in_sz]
// ---------------------------------------------------------------------------
void CNNEval::fc(const float* in, int in_sz,
                 float* out, int out_sz,
                 const float* w, const float* b) const {
    for (int o = 0; o < out_sz; ++o) {
        float sum = b[o];
        const float* row = w + o * in_sz;
        for (int i = 0; i < in_sz; ++i)
            sum += row[i] * in[i];
        out[o] = sum;
    }
}

// ---------------------------------------------------------------------------
// evaluate
// ---------------------------------------------------------------------------
float CNNEval::evaluate(uint64_t bb0, uint64_t bb1, uint64_t holes) const {
    // Build input tensor [C1_IN][H][W] = [3][8][8], stored flat channel-first.
    float input[C1_IN * H * W] = {};
    for (int sq = 0; sq < 64; ++sq) {
        if ((bb0   >> sq) & 1) input[0 * 64 + sq] = 1.0f;
        if ((bb1   >> sq) & 1) input[1 * 64 + sq] = 1.0f;
        if ((holes >> sq) & 1) input[2 * 64 + sq] = 1.0f;
    }

    // Conv1: [3][8][8] -> [16][8][8]
    float map1[C1_OUT * H * W];
    conv2d(input, C1_IN, map1, C1_OUT, conv1w.data(), conv1b.data());

    // Conv2: [16][8][8] -> [32][8][8]
    float map2[C2_OUT * H * W];
    conv2d(map1, C1_OUT, map2, C2_OUT, conv2w.data(), conv2b.data());

    // FC1: 2048 -> 64, ReLU
    float hidden[FC1_OUT];
    fc(map2, FLAT, hidden, FC1_OUT, fc1w.data(), fc1b.data());
    for (int i = 0; i < FC1_OUT; ++i) hidden[i] = relu(hidden[i]);

    // FC2: 64 -> 1
    float raw;
    fc(hidden, FC1_OUT, &raw, 1, fc2w.data(), &fc2b);

    // tanh scaled to [-SCORE_SCALE, +SCORE_SCALE]
    return std::tanh(raw) * SCORE_SCALE;
}

// ---------------------------------------------------------------------------
// Weight binary format:
//   Magic: "CNNW" (4 bytes)
//   Version: uint32_t = 1
//   All weight arrays in declaration order, raw float32 LE.
// ---------------------------------------------------------------------------
static constexpr uint32_t MAGIC   = 0x574E4E43; // "CNNW"
static constexpr uint32_t VERSION = 1;

bool CNNEval::loadWeights(const std::string& path) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;

    uint32_t magic, version;
    if (std::fread(&magic,   sizeof(magic),   1, f) != 1 || magic   != MAGIC)   { std::fclose(f); return false; }
    if (std::fread(&version, sizeof(version), 1, f) != 1 || version != VERSION) { std::fclose(f); return false; }

    auto rd = [&](float* ptr, size_t n) -> bool {
        return std::fread(ptr, sizeof(float), n, f) == n;
    };

    bool ok = rd(conv1w.data(), conv1w.size())
           && rd(conv1b.data(), conv1b.size())
           && rd(conv2w.data(), conv2w.size())
           && rd(conv2b.data(), conv2b.size())
           && rd(fc1w.data(),   fc1w.size())
           && rd(fc1b.data(),   fc1b.size())
           && rd(fc2w.data(),   fc2w.size())
           && rd(&fc2b, 1);

    std::fclose(f);
    return ok;
}

bool CNNEval::saveWeights(const std::string& path) const {
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;

    std::fwrite(&MAGIC,   sizeof(MAGIC),   1, f);
    std::fwrite(&VERSION, sizeof(VERSION), 1, f);

    auto wr = [&](const float* ptr, size_t n) {
        std::fwrite(ptr, sizeof(float), n, f);
    };

    wr(conv1w.data(), conv1w.size());
    wr(conv1b.data(), conv1b.size());
    wr(conv2w.data(), conv2w.size());
    wr(conv2b.data(), conv2b.size());
    wr(fc1w.data(),   fc1w.size());
    wr(fc1b.data(),   fc1b.size());
    wr(fc2w.data(),   fc2w.size());
    wr(&fc2b, 1);

    std::fclose(f);
    return true;
}
