#pragma once
#include "AIBase.hpp"
#include <string>
#include <sys/types.h>

// Lance le binaire IA comme processus fils et communique via stdin/stdout.
// Protocole envoyé :  "<W> <H> <player>\n<cell0> <cell1> ...\n"
// Protocole reçu  :  "((x1,y1),(x2,y2))\n"
class AIProcess : public AIBase {
public:
    // algo  : "random" | "ab" | "ab_dyn" | "negamax"
    // depth : profondeur de recherche (ignoré pour random)
    AIProcess(const std::string& binaryPath,
              const std::string& algo  = "negamax",
              int                depth = 4);
    ~AIProcess() override;

    Move        chooseMove(const Board& b, Player p) override;
    std::string name() const override { return "AIProcess(" + name_ + ")"; }

private:
    void sendBoard(const Board& b, Player p);
    Move readMove();

    pid_t       pid_    = -1;
    int         toAI_   = -1;   // parent écrit  → fils lit   (stdin du fils)
    int         fromAI_ = -1;   // fils écrit     → parent lit (stdout du fils)
    std::string name_;
};
