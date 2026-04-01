cmake_minimum_required(VERSION 3.16)
project(Blobwar CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ==============================================================================
# OPTIONS CONFIGURABLES
# Passer via : cmake -D<OPTION>=ON/OFF ..
# ==============================================================================

option(BLOBWAR_STATIC_SFML
    "Lier SFML en statique (recommandé si SFML local)" OFF)

# ==============================================================================
# SFML — résolution flexible
#
#  Priorité :
#   1. -DSFML_DIR=<chemin>          → override total (chemin arbitraire)
#   2. lib/SFML/ détecté dans le dépôt → utilisé automatiquement
#   3. Défaut                        → CMake cherche dans les chemins système
#
#  Dans tous les cas, -DBLOBWAR_STATIC_SFML=ON force l'édition statique.
# ==============================================================================

set(_LOCAL_SFML_CMAKE "${CMAKE_SOURCE_DIR}/lib/SFML/lib/cmake/SFML")

if(SFML_DIR)
    # Chemin fourni explicitement — on ne touche à rien
    message(STATUS "[Blobwar] SFML_DIR fourni manuellement : ${SFML_DIR}")

elseif(EXISTS "${_LOCAL_SFML_CMAKE}")
    # lib/SFML/ présent dans le dépôt → priorité sur le système
    set(SFML_DIR "${_LOCAL_SFML_CMAKE}")
    set(BLOBWAR_STATIC_SFML ON)   # local = statique par défaut
    message(STATUS "[Blobwar] SFML local détecté et utilisé : ${SFML_DIR}")

else()
    message(STATUS "[Blobwar] lib/SFML/ absent — recherche dans les chemins système")
endif()

if(BLOBWAR_STATIC_SFML)
    set(SFML_STATIC_LIBRARIES TRUE)
endif()

find_package(SFML 3 REQUIRED COMPONENTS Graphics Window System)

# ==============================================================================
# TBB — Intel Threading Building Blocks
# ==============================================================================

find_package(TBB REQUIRED)

# ==============================================================================
# CIBLES
# ==============================================================================

# --- Moteur (aucune dépendance externe) ---
add_library(blobwar_engine STATIC
    src/engine/Board.cpp
    src/engine/Rules.cpp
    src/engine/MoveGen.cpp
)
target_include_directories(blobwar_engine PUBLIC src/engine)

# --- Évaluation partagée ---
add_library(blobwar_eval STATIC
    src/ai/Eval/Eval.cpp
    src/ai/Eval/CNNEval.cpp
)
target_include_directories(blobwar_eval PUBLIC src/ai/Eval)
target_link_libraries(blobwar_eval PUBLIC blobwar_engine)

# --- IA (dépend du moteur et de TBB, pas de SFML) ---
add_library(blobwar_ai STATIC
    src/ai/Random/RandomAI.cpp
    src/ai/AlphaBeta/AlphaBetaAI.cpp
    src/ai/Negamax/NegamaxAI.cpp
    src/ai/NegamaxParallel/NegamaxParallelAI.cpp
    src/ai/NegamaxYBW/NegamaxYBWAI.cpp
    src/ai/NegamaxParInc/NegamaxParIncAI.cpp
    src/ai/AIProcess.cpp
)
target_include_directories(blobwar_ai PUBLIC
    src/ai
    src/engine
    src/ai/Eval
    src/ai/Random
    src/ai/AlphaBeta
    src/ai/Negamax
    src/ai/NegamaxParallel
    src/ai/NegamaxYBW
    src/ai/NegamaxParInc
)
target_link_libraries(blobwar_ai PUBLIC blobwar_eval TBB::tbb)

# --- Interface graphique ---
add_library(blobwar_ui STATIC
    src/ui/Renderer.cpp
    src/ui/App.cpp
)
target_include_directories(blobwar_ui PUBLIC src/ui)
target_link_libraries(blobwar_ui PUBLIC
    blobwar_engine
    blobwar_ai
    SFML::Graphics
    SFML::Window
    SFML::System
)

# --- Exécutable principal ---
add_executable(blobwar src/main.cpp)
target_include_directories(blobwar PRIVATE src/ui)
target_link_libraries(blobwar PRIVATE blobwar_ui blobwar_ai)

# --- Processus fils IA (binaire séparé) ---
add_executable(ai_stub ai_stub/main.cpp)
target_link_libraries(ai_stub PRIVATE blobwar_ai)

# --- Benchmark des IA ---
add_executable(bench bench/main.cpp bench/BenchAPI.cpp)
target_include_directories(bench PRIVATE bench src/ai/NegamaxVariants/YBWsearchZobrist src/ai/NegamaxParInc src/ai/NegamaxVariants/YBWsearchZobrist)
target_link_libraries(bench PRIVATE blobwar_ai)

# --- Optimiseur de paramètres NegamaxParIncAIStud ---
add_executable(optimizer optimizer/main.cpp optimizer/Optimizer.cpp)
target_include_directories(optimizer PRIVATE optimizer src/ai/NegamaxParInc)
target_link_libraries(optimizer PRIVATE blobwar_ai)

# ==============================================================================
# RÉCAPITULATIF AU MOMENT DU CONFIGURE
# ==============================================================================
message(STATUS "----------------------------------------------------")
message(STATUS "  Blobwar — configuration résumée")
message(STATUS "  SFML_DIR           : ${SFML_DIR}")
message(STATUS "  SFML statique      : ${SFML_STATIC_LIBRARIES}")
message(STATUS "  BLOBWAR_SFML_LOCAL : ${BLOBWAR_SFML_LOCAL}")
message(STATUS "  Compilateur        : ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "  Build type         : ${CMAKE_BUILD_TYPE}")
message(STATUS "----------------------------------------------------")