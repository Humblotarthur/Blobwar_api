#pragma once
#include <cstdint>
#include <vector>

struct TrainSample {
    uint64_t bb0;
    uint64_t bb1;
    uint64_t holes;
    float    target;
};

using TrainBatch = std::vector<TrainSample>;
