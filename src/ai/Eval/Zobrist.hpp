#pragma once
#include <cstdint>
#include <cstring>
#include <random>
#include "IncMoveState.hpp"

// ─────────────────────────────────────────────────────────────────────────────
// Flags TT
// ─────────────────────────────────────────────────────────────────────────────
constexpr uint8_t TT_EXACT = 0; // score exact
constexpr uint8_t TT_LOWER = 1; // lower bound  (fail-high / beta cutoff)
constexpr uint8_t TT_UPPER = 2; // upper bound  (fail-low  / alpha cutoff)

// ─────────────────────────────────────────────────────────────────────────────
// TTEntry — 16 bytes (aligné cache, 4 entrées par ligne de 64 bytes)
//
// Pas de mutex : les races entre threads sont acceptées (last-write-wins).
// La vérification key == hash protège contre les lectures corrompues.
// ─────────────────────────────────────────────────────────────────────────────
struct alignas(16) TTEntry {
    uint64_t key;    // hash complet pour vérifier la collision
    int16_t  score;
    int8_t   depth;
    uint8_t  flag;
    uint32_t _pad;   // padding → 16 bytes
};

// ─────────────────────────────────────────────────────────────────────────────
// ZobristTT — table de transposition complète + clés Zobrist
//
// Utilisation :
//   ZobristTT* ztt = new ZobristTT(seed);
//   uint64_t h = ztt->computeHash(bb[0], bb[1], pi);
//   h = ztt->applyHash(h, m, blobMask, pi, oi, w, neighborMask[dst]);
//   ztt->store(h, score, depth, flag);
//   const TTEntry* e = ztt->probe(h);
//
// Alloué sur le heap (sizeof ≈ TABLE_SIZE * 16 bytes = 16 MB pour 1M entrées).
// ─────────────────────────────────────────────────────────────────────────────
struct ZobristTT {

    static constexpr int TABLE_BITS = 20;              // 1M entrées
    static constexpr int TABLE_SIZE = 1 << TABLE_BITS; // 16 MB

    // Clés Zobrist : pieces[player][square]
    uint64_t pieces[2][64];
    uint64_t sideToMove; // XOR pour basculer le joueur courant

    // Table de transposition (allouée inline → pas de double indirection)
    TTEntry table[TABLE_SIZE];

    // ── Construction ─────────────────────────────────────────────────────────
    explicit ZobristTT(uint64_t seed = 0xDEADBEEFCAFEBABEULL) { init(seed); }

    void init(uint64_t seed = 0xDEADBEEFCAFEBABEULL) {
        std::mt19937_64 rng(seed);
        for (int p = 0; p < 2; ++p)
            for (int sq = 0; sq < 64; ++sq)
                pieces[p][sq] = rng();
        sideToMove = rng();
        // Réinitialise la table (clés à 0 = entrée invalide)
        std::memset(table, 0, sizeof(table));
    }

    // ── Hash initial depuis les bitboards ────────────────────────────────────
    inline uint64_t computeHash(uint64_t bb0, uint64_t bb1, int pi) const {
        uint64_t h = 0;
        uint64_t tmp;
        tmp = bb0;
        while (tmp) { int sq = __builtin_ctzll(tmp); tmp &= tmp-1; h ^= pieces[0][sq]; }
        tmp = bb1;
        while (tmp) { int sq = __builtin_ctzll(tmp); tmp &= tmp-1; h ^= pieces[1][sq]; }
        if (pi == 1) h ^= sideToMove; // P2 joue en premier
        return h;
    }

    // ── Mise à jour incrémentale du hash après applyMove ─────────────────────
    //
    // Doit être appelé APRÈS applyMove (blobMask est connu).
    // XOR est son propre inverse → removeMove = appeler cette même fonction.
    //
    // Paramètres :
    //   h          : hash avant le coup
    //   m          : coup joué
    //   blobMask   : masque retourné par applyMove (pièces converties)
    //   pi / oi    : indices joueur courant / adversaire (0 ou 1)
    //   w          : largeur du plateau
    //   nmask_dst  : neighborMask[dst] — masque des 8 voisins de la destination
    //
    // Retourne le nouveau hash.
    inline uint64_t applyHash(uint64_t h,
                               const Move& m,
                               int8_t blobMask,
                               int pi, int oi,
                               int w,
                               uint64_t nmask_dst) const
    {
        const int src = m.y1 * w + m.x1;
        const int dst = m.y2 * w + m.x2;
        const bool jump = ((m.x2-m.x1)*(m.x2-m.x1) + (m.y2-m.y1)*(m.y2-m.y1)) > 2;

        // Pièce posée en dst
        h ^= pieces[pi][dst];

        // Si saut : pièce retirée de src
        if (jump) h ^= pieces[pi][src];

        // Pièces converties : change d'ownership
        uint64_t tmp = nmask_dst;
        int bit = 0;
        while (tmp) {
            const int sq = __builtin_ctzll(tmp);
            tmp &= tmp - 1;
            if ((blobMask >> bit) & 1) {
                h ^= pieces[oi][sq]; // retire de l'adversaire
                h ^= pieces[pi][sq]; // ajoute au joueur courant
            }
            ++bit;
        }

        // Bascule le joueur courant
        h ^= sideToMove;
        return h;
    }

    // ── Probe ────────────────────────────────────────────────────────────────
    // Retourne nullptr si miss ou entrée corrompue (clé différente).
    inline const TTEntry* probe(uint64_t hash) const {
        const TTEntry& e = table[hash & (TABLE_SIZE - 1)];
        return (e.key == hash) ? &e : nullptr;
    }

    // ── Store — lock-free, last-write-wins ───────────────────────────────────
    // Stratégie : on remplace toujours si profondeur supérieure ou égale.
    // Les races entre threads sont acceptées : au pire on perd une écriture.
    inline void store(uint64_t hash, int16_t score, int8_t depth, uint8_t flag) {
        TTEntry& e = table[hash & (TABLE_SIZE - 1)];
        // Remplacement si entrée vide, même profondeur ou plus profonde
        if (e.key == 0 || e.depth <= depth) {
            e.score = score;
            e.depth = depth;
            e.flag  = flag;
            e.key   = hash; // écrit en dernier pour minimiser la fenêtre de corruption
        }
    }
};