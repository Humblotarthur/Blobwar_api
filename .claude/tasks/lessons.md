# Lessons & Mistakes

## Optimizer Phase 2 — Condition d'arrêt asymétrique

**Problème** : L'ancienne Phase 2 validait un nouveau best si `winRate > 0.5` (1 win suffit sur 2 parties),
mais stoppait le paramètre seulement après 3 victoires CONSÉCUTIVES de l'ancien modèle.
Avec des modèles de force quasi-égale, 3 wins consécutifs = quasi-impossible → boucle infinie.

**Fix** : Incrément fixe (pas d'exponentiel), 5 parties décident le gagnant (majoritaire),
compteur d'échecs TOTAL (pas consécutif), stop après 3 échecs totaux.

**Règle** : Pour les conditions d'arrêt d'optimizer, toujours vérifier la symétrie win/loss.
Si les modèles ont une force similaire, les conditions doivent être équilibrées.

---

## PMR : coupure dure vs null-window

**Observation** : Couper brutalement les derniers X% de coups (par rang statique) est moins robuste
que vérifier avec une null-window à depth 1 si le coup peut battre alpha.
La null-window préserve les coups qui semblent mauvais statiquement mais sont tactiquement bons.

**Fix** : `nwScore = -searchZobrist(1, opp, -(alpha+1), -alpha, hash)` en place après move 0.
Coûte un peu plus (depth-1 sequential) mais donne une meilleure sélection.

---

## Subagent pour réécriture de fichiers complexes

**Observation** : Pour modifier simultanément plusieurs fichiers similaires (3 variantes d'un algo),
lancer un subagent évite de polluer le contexte principal avec du code répétitif.

**Règle** : Code volumineux répétitif → subagent. Modifications ciblées → Edit direct.
