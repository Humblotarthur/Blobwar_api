#pragma once
// NegamaxParIncAIDynStudRandom
//
// Epsilon-greedy wrapper autour de NegamaxParIncAIDynStud.
// À chaque coup, joue aléatoirement avec probabilité cfg.randomEpsilon,
// sinon délègue à NegamaxParIncAIDynStud (notre algorithme le plus avancé).
// Utilisé pour générer de la diversité lors de l'entraînement du CNN.

#include "AIBase.hpp"
#include "NegamaxParIncAIDynStud.hpp"
#include "RandomAI.hpp"
#include "NegamaxConfig.hpp"
#include <random>
#include <string>

class NegamaxParIncAIDynStudRandom : public AIBase {
public:
    explicit NegamaxParIncAIDynStudRandom(NegamaxConfig cfg)
        : inner_(cfg), epsilon_(cfg.randomEpsilon), timeLimitMs_(cfg.timeLimitMs),
          rng_(std::random_device{}()), dist_(0.0, 1.0) {}

    std::string name() const override {
        return "NegamaxParIncAIDynStudRandom(t="
             + std::to_string(timeLimitMs_) + "ms"
             + ",eps=" + std::to_string(epsilon_) + ")";
    }

    Move chooseMove(const Board& b, Player p) override {
        if (dist_(rng_) < epsilon_)
            return random_.chooseMove(b, p);
        return inner_.chooseMove(b, p);
    }

private:
    NegamaxParIncAIDynStud inner_;
    RandomAI               random_;
    double                 epsilon_;
    int                    timeLimitMs_;
    std::mt19937           rng_;
    std::uniform_real_distribution<double> dist_;
};
