#pragma once
#include <string>
#include <fstream>
#include <sstream>
#include <stdexcept>

// ─────────────────────────────────────────────────────────────────────────────
// NegamaxConfig
//
// Paramètres runtime de NegamaxParIncAI.
// Chargement via fichier key=value (# pour commentaires).
// Valeurs par défaut identiques aux anciens static constexpr.
// ─────────────────────────────────────────────────────────────────────────────
struct NegamaxConfig {
    int    timeLimitMs             = 1000;
    int    maxDepth                = 20;
    int    parallelThreshold       = 2;
    int    sortDepth               = 2;
    int    lmrMinDepth             = 3;
    bool   useLogReduction         = true;
    double lmrAlpha                = 0.5;
    int    lmrK                    = 12;
    bool   lmrNullWindowBeforeFull = true;
    bool   lmpEnabled              = false;
    double lmpPruneRatio           = 0.25;
    bool   useCnnEval              = false;

    // PMR — Pruning Move Ratio (DynStud variants)
    double pmrRatio    = 0.2;   // proportion of worst moves pruned (0 = disabled)
    int    pmrMinDepth = 3;     // min depth to activate PMR

    // LMR simple (DynStud variants)
    bool   lmrEnabled  = true;  // enable LMR
    bool   lmrResearch = true;  // re-search at full depth if reduced score > alpha

    // Charge depuis un fichier key=value.
    // Ignore les lignes vides et les commentaires (#).
    // Lève std::runtime_error si le fichier ne peut pas être ouvert.
    static NegamaxConfig loadFromFile(const std::string& path) {
        std::ifstream f(path);
        if (!f.is_open())
            throw std::runtime_error("NegamaxConfig: cannot open '" + path + "'");

        NegamaxConfig cfg;
        std::string line;
        while (std::getline(f, line)) {
            // strip commentaire
            const auto hash = line.find('#');
            if (hash != std::string::npos) line = line.substr(0, hash);

            const auto eq = line.find('=');
            if (eq == std::string::npos) continue;

            std::string key = trim(line.substr(0, eq));
            std::string val = trim(line.substr(eq + 1));
            if (key.empty() || val.empty()) continue;

            if      (key == "time_limit_ms")            cfg.timeLimitMs             = std::stoi(val);
            else if (key == "max_depth")                cfg.maxDepth                = std::stoi(val);
            else if (key == "parallel_threshold")       cfg.parallelThreshold       = std::stoi(val);
            else if (key == "sort_depth")               cfg.sortDepth               = std::stoi(val);
            else if (key == "lmr_min_depth")            cfg.lmrMinDepth             = std::stoi(val);
            else if (key == "use_log_reduction")        cfg.useLogReduction         = parseBool(val);
            else if (key == "lmr_alpha")                cfg.lmrAlpha                = std::stod(val);
            else if (key == "lmr_k")                    cfg.lmrK                    = std::stoi(val);
            else if (key == "lmr_nullwindow_before_full") cfg.lmrNullWindowBeforeFull = parseBool(val);
            else if (key == "lmp_enabled")              cfg.lmpEnabled              = parseBool(val);
            else if (key == "lmp_prune_ratio")          cfg.lmpPruneRatio           = std::stod(val);
            else if (key == "use_cnn_eval")             cfg.useCnnEval              = parseBool(val);
            else if (key == "pmr_ratio")                cfg.pmrRatio                = std::stod(val);
            else if (key == "pmr_min_depth")            cfg.pmrMinDepth             = std::stoi(val);
            else if (key == "lmr_enabled")              cfg.lmrEnabled              = parseBool(val);
            else if (key == "lmr_research")             cfg.lmrResearch             = parseBool(val);
        }
        return cfg;
    }

    // Sauvegarde dans un fichier key=value réutilisable par loadFromFile.
    void save(const std::string& path) const {
        std::ofstream f(path);
        if (!f) throw std::runtime_error("NegamaxConfig: cannot write '" + path + "'");
        f << "time_limit_ms              = " << timeLimitMs             << "\n"
          << "max_depth                  = " << maxDepth                << "\n"
          << "parallel_threshold         = " << parallelThreshold       << "\n"
          << "sort_depth                 = " << sortDepth               << "\n"
          << "lmr_min_depth              = " << lmrMinDepth             << "\n"
          << "use_log_reduction          = " << (useLogReduction ? "true" : "false") << "\n"
          << "lmr_alpha                  = " << lmrAlpha                << "\n"
          << "lmr_k                      = " << lmrK                    << "\n"
          << "lmr_nullwindow_before_full = " << (lmrNullWindowBeforeFull ? "true" : "false") << "\n"
          << "lmp_enabled                = " << (lmpEnabled ? "true" : "false") << "\n"
          << "lmp_prune_ratio            = " << lmpPruneRatio           << "\n"
          << "use_cnn_eval               = " << (useCnnEval ? "true" : "false") << "\n"
          << "pmr_ratio                  = " << pmrRatio                << "\n"
          << "pmr_min_depth              = " << pmrMinDepth             << "\n"
          << "lmr_enabled                = " << (lmrEnabled ? "true" : "false") << "\n"
          << "lmr_research               = " << (lmrResearch ? "true" : "false") << "\n";
    }

private:
    static std::string trim(std::string s) {
        const char* ws = " \t\r\n";
        s.erase(0, s.find_first_not_of(ws));
        const auto last = s.find_last_not_of(ws);
        if (last != std::string::npos) s.erase(last + 1);
        else s.clear();
        return s;
    }
    static bool parseBool(const std::string& s) {
        return s == "1" || s == "true" || s == "yes";
    }
};
