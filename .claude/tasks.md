# Task 1 - Changing some IA by Dynamique IA

IA search for a certain depth and choose the best play found. I want a different model, the IA have a limit time to find the best possible play. Until they're not stopped they look for a better play. We reuse the result of the previous iteration to guide the next search, which is why we still go from depth 4 to depth 5 and so on until the max time. The key difference is that we reorder the moves using information already found, placing the best move from the previous iteration at the front of the move ordering (YBW), so it is explored first. This greatly improves alpha-beta pruning efficiency because good moves are searched earlier, leading to more cutoffs and a faster overall search. The killer heuristic is a move ordering technique used in alpha-beta search to improve pruning efficiency. It assumes that moves causing a beta-cutoff in one position are likely to be strong in similar positions at the same depth. For each depth level, you store one or two “killer moves” that previously caused cutoffs. When exploring a new node at that depth, you try these killer moves early, right after the best move and captures. These moves are not captures, since captures are usually handled separately. If a killer move again causes a beta-cutoff, it reinforces its usefulness. This helps the search find good cutoffs earlier, reducing the number of nodes explored. The heuristic is simple to implement and very fast. Typically, you maintain a small table indexed by depth. When a new cutoff occurs, you update the stored killer moves. Overall, it significantly improves move ordering with minimal overhead. Stock a maximum of 10 mooves per layer. for each moove stock a single int64bits, with all 0, and a 1 for the blob to moove and a 1 to the case it will moove, there's no ambiguity because the blob need an empty case to jump. 

I want you to change thoose models: 

- YBWsearch ( create a new model YBWsearchDYN)
- NegamaxParINCAISutd change the current file while keeping the way to easily parametrise
- NegamaxParIncAI ( create a new model in a new file NegamaxparIncAIDYN )

Already did i let explanations


# Task 2 — Advanced AI Testing API

Fixing the existing optimizer module. I want an optimizer that automatically runs matches between NegamaxParIncAIStudDYN and a YBW search with dynamic settings, where each move is computed within a 0.25 second time limit. For each iteration, we reduce all “elagation rate” parameters by half and run matches until we find a configuration in NegamaxParIncAIStudDYN that consistently beats the YBW search. Once such parameters are found, we start a second phase where the new model plays against the previously best saved model, and we progressively increase some elagation parameters using an exponential backoff strategy as long as it keeps winning. Each time a model wins, it is saved as the new best version, and we also verify that it still outperforms the YBW dynamic search. If the model starts losing after increasing parameters, we reset to a smaller increment and repeat the process. Once we find stable parameters (e.g. three consecutive wins against the improving model), we move on to optimizing the next parameter. We begin with LMR_MIN_DEPTH, LMR_ALPHA, LMR_K, and LMP_PRUNE_RATIO, keeping LMP_PRUNE_RATIO set to 0 in earlier optimization stages until we specifically tune it.

You can make really majore changement in the optimizer packag for this one.

Already did i let explanations becasue will need some change after task 3


# Task 3 — Rebuild NegamaxParIncAIDynStud depuis YBWsearchZobristDYN

## Contexte / Diagnostic

NegamaxParIncAIDynStud (stud_zero) perd 0-10 contre ybw_dyn alors que
YBWsearchZobristDYN (même structure de base) bat ybw_dyn 7-3.
=> NegamaxParIncAIDynStud est cassé.

NegamaxParIncAIDynStud est supposé être une copie propre de l'algo de référence
(ybwz_dyn) sur laquelle on implémente LMR + PMR pour l'optimiser.

## Objectif

Repartir de YBWsearchZobristDYN comme base et implémenter LMR + PMR
de manière propre, paramétrable et mathématiquement cohérente.

Fichier cible : src/ai/NegamaxParInc/NegamaxParIncAIDynStud.hpp
(réécriture complète à partir de YBWsearchZobristDYN)

## Architecture des réductions

### Tri des coups à la racine
- Tri par eval statique (simple, comme ybwz_dyn)
- Les coups sont ordonnés du meilleur au pire : move[0] = meilleur, move[n-1] = pire

### PMR — Pruning Move Ratio (proportion fixe)
- Les PMR_RATIO derniers coups (pourcentage de n) sont complètement élagués (non explorés)
- Seuls les (1 - PMR_RATIO) * n premiers coups sont explorés
- Paramètre : double pmrRatio = 0.2  (ex: 20% des pires coups ignorés)
- S'applique uniquement si depth >= pmrMinDepth
- Paramètre : int pmrMinDepth = 3

### LMR — Late Move Reduction (proportion logarithmique, multi-classes)

Principe : les coups sont divisés en K classes selon leur rang dans l'ordre.
La classe 0 (meilleurs coups) est recherchée à pleine profondeur.
Les classes suivantes sont recherchées à profondeur réduite.

#### Calcul du nombre de classes K
K = max(1, floor(log2(n_moves_explored)))
Attention la fonction sert à trouver une proportion de coup dans chaque catégorie, seulement 4 catégorie, coup supprimé profondeur maximum, coup profondeur -2, coup profondeur -4, profondeur 3. ( ici pour pronfondeur 9 par exemple. Si profondeur moins de 8 toujours à fond) Si un coup moins cherché pourrait être bien, on le recherche immédiatement à profondeur de la classe supérieur, ect. 
Pour décider si un coup est potentiellement bien, on cherche plutôt à regarder si le coup est effectivement mauvais, si on arrive à une position ou le score est effectivement négatif depuis 2 ou 3 coups (plutôt prendre inférieur ou égale à 0 ) Attention plutôt que 0 prendre le score initial du plateau estimé avecla fonction de coup. on peut immédiatement arreter sa recherche. Si le score est alternant fortement (-2 +2)-> profondeur supérieur idem si score positif à plus de 1 dernier coup la position reste stable avec un score négatif. ( les deux derniers coups avec un score négatif ). Aussi prendre la règle de score > alpha en dessous en compte.

Exemple : 8 coups → K=3, 16 coups → K=4, 4 coups → K=2


#### Attribution des classes (partition logarithmique)
  classe 0 : 1er coup (always full depth, PV node)
  classe 1 : coups 2..floor(n/2)
  classe 2 : coups floor(n/2)+1..floor(3n/4)
  ... chaque classe suivante contient moitié moins de coups

#### Réduction par classe
  reduction(classe i) = i   [fonction f(i) customisable en une ligne]
  depth_searched = max(1, depth - 1 - reduction(i))

#### Re-search si coup réduit semble bon
Si score_réduit > alpha → relancer à pleine profondeur pour confirmer
Contrôlé par : bool lmrResearch = true

### Parallélisme (YBW pattern)
- Coup 0 toujours séquentiel (PV)
- Coups 1..n-PMR en parallèle (TBB), réduction appliquée dans le worker

## Paramètres à ajouter dans NegamaxConfig
  double pmrRatio    = 0.2   # proportion coups élagués (PMR)
  int    pmrMinDepth = 3     # profondeur min PMR
  bool   lmrEnabled  = true  # activer LMR
  int    lmrMinDepth = 2     # profondeur min LMR
  bool   lmrResearch = true  # re-search pleine profondeur si coup réduit bon

## Ce que l'implémentation NE doit PAS inclure (version de base)
- Pas de sortDepth (sort par mini-search) → trop coûteux
- Pas de killer heuristic
- Pas de prevBestMove
- Sort uniquement par eval statique

## Validation attendue
1. stud_zero (pmrRatio=0, lmrEnabled=false) ~= ybwz_dyn (~70% vs ybw_dyn)
2. stud_dyn (PMR+LMR activés) bat stud_zero

## Fichiers de référence à lire
- src/ai/NegamaxVariants/YBWsearchZobrist/YBWsearchZobristDYN.hpp  ← BASE
- src/ai/NegamaxParInc/NegamaxConfig.hpp                           ← à étendre
- src/ai/NegamaxParInc/NegamaxParIncAIDynStud.hpp                  ← à remplacer


Plutôt que de m'implementer tous l'algorithme d'un coup fait moi:
-> un fichier avec ajout PMR
-> un fichier avec ajout PMR et LMR seulement 2 classes
-> un fichier avec ajout PMR et LMR avec ajout dynamique.
