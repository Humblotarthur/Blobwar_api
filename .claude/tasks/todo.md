# Task Progress

## Task 3 — Rebuild NegamaxParIncAIDynStud ✅

- [x] Lire YBWsearchZobristDYN.hpp comme base de référence
- [x] Étendre NegamaxConfig : pmrRatio, pmrMinDepth, lmrEnabled, lmrResearch
- [x] Écrire NegamaxParIncAIDynStudPMR.hpp (PMR null-window uniquement)
- [x] Écrire NegamaxParIncAIDynStudPMR_LMR2.hpp (PMR + LMR 2 classes fixes)
- [x] Réécrire NegamaxParIncAIDynStud.hpp (PMR + LMR K-classes dynamique)
- [x] Validation compilation : tous les targets buildent proprement
- [x] PMR null-window : remplacer coupure dure par vérification -searchZobrist(1, opp, -(alpha+1), -alpha)
- [x] Benchmark pmr_zero/lmr2_zero/dyn_zero vs ybw_dyn 250ms — logique de base validée
- [x] Benchmark pmr_dyn (pmrRatio=0.2) vs ybw_dyn : 4W/1L → PMR améliore

## Task 2 — Optimizer ✅ (partiel)

- [x] Réécrire Optimizer.cpp/hpp pour NegamaxParIncAIDynStud_PMR
- [x] Phase 1 : grid search pmrRatio × pmrMinDepth
- [x] Phase 2 : nouveau logic — incrément fixe, 3 échecs TOTAUX (pas consécutifs) stop
- [x] Ajout pmr_dyn / pmr_zero / lmr2_zero / dyn_zero dans BenchAPI
- [x] Run optimizer 250ms/1 partie → best : pmrRatio=0.55, pmrMinDepth=6
- [ ] Run optimizer 500ms/5 parties avec nouveaux paramètres Phase 2
- [ ] Optimizer NegamaxParIncAIDynStud (PMR + LMR) — tâche suivante

## Review

### Résultats bench (250ms, classic8)
| Modèle | Score vs ybw_dyn |
|---|---|
| ybwz_dyn (référence) | ~2/3 |
| pmr_zero | 3W/1L/1D sur 5 |
| pmr_dyn (ratio=0.2) | 4W/1L sur 5 |

### Architecture ajoutée
- `NegamaxParIncAIDynStudPMR` : PMR null-window + iterative deepening + Zobrist TT
- `NegamaxParIncAIDynStudPMR_LMR2` : + LMR 2 classes (reduction=1 pour tous)
- `NegamaxParIncAIDynStud` (rebuild) : + LMR K-classes (K=floor(log2(n)), partition log)
- `NegamaxConfig` : +pmrRatio, +pmrMinDepth, +lmrEnabled, +lmrResearch
- `Optimizer` : réécrit pour PMR, Phase 2 logique corrigée
