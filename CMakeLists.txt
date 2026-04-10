cmake_minimum_required(VERSION 3.16)
project(Blobwar CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ==============================================================================
# Build type par défaut
# ==============================================================================

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS Debug Release RelWithDebInfo MinSizeRel)
endif()

# ==============================================================================
# OPTIONS CONFIGURABLES
# ==============================================================================

option(BLOBWAR_STATIC_SFML
    "Lier SFML en statique (recommandé si SFML local)" OFF)

# ==============================================================================
# SFML — résolution flexible
#
#  Priorité :
#   1. -DSFML_DIR=<chemin>          → override total (chemin arbitraire)
#   2. lib/SFML/ détecté dans le dépôt → utilisé automatiquement (statique)
#   3. Défaut                        → CMake cherche dans les chemins système
# ==============================================================================

set(_LOCAL_SFML_CMAKE "${CMAKE_SOURCE_DIR}/lib/SFML/lib/cmake/SFML")

if(SFML_DIR)
    message(STATUS "[Blobwar] SFML_DIR fourni manuellement : ${SFML_DIR}")
    set(_BLOBWAR_SFML_SOURCE "manuel")

elseif(EXISTS "${_LOCAL_SFML_CMAKE}")
    set(SFML_DIR "${_LOCAL_SFML_CMAKE}")
    set(BLOBWAR_STATIC_SFML ON)
    set(_BLOBWAR_SFML_SOURCE "lib/SFML (local, statique)")
    message(STATUS "[Blobwar] SFML local détecté : ${SFML_DIR}")

else()
    set(_BLOBWAR_SFML_SOURCE "système")
    message(STATUS "[Blobwar] lib/SFML/ absent — recherche dans les chemins système")
    message(STATUS "[Blobwar] Si SFML est introuvable, lancez : bash setupenv.sh")
endif()

if(BLOBWAR_STATIC_SFML)
    set(SFML_STATIC_LIBRARIES TRUE)
endif()

find_package(SFML 3 REQUIRED COMPONENTS Graphics Window System)

# ==============================================================================
# TBB — Intel Threading Building Blocks
# ==============================================================================

find_package(TBB REQUIRED)

if(NOT TBB_FOUND)
    message(FATAL_ERROR
        "[Blobwar] TBB introuvable.\n"
        "  Linux  : sudo apt install libtbb-dev\n"
        "  macOS  : brew install tbb\n"
        "  Ou lancez : bash setupenv.sh")
endif()

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
target_include_directories(bench PRIVATE bench src/ai/NegamaxVariants/YBWsearchZobrist src/ai/NegamaxParInc)
target_link_libraries(bench PRIVATE blobwar_ai)

# --- Générateur de datasets d'entraînement CNN ---
add_executable(data_gen data_gen/main.cpp)
target_include_directories(data_gen PRIVATE
    src/ai/NegamaxVariants/YBWsearchZobrist
    src/ai/NegamaxParInc
    src/ai
    src/engine
    src/ai/Eval
)
target_link_libraries(data_gen PRIVATE blobwar_ai)

# --- Optimiseur de paramètres NegamaxParIncAIStud ---
add_executable(optimizer optimizer/main.cpp optimizer/Optimizer.cpp)
target_include_directories(optimizer PRIVATE optimizer src/ai/NegamaxParInc)
target_link_libraries(optimizer PRIVATE blobwar_ai)

# ==============================================================================
# RÉCAPITULATIF AU MOMENT DU CONFIGURE
# ==============================================================================
message(STATUS "----------------------------------------------------")
message(STATUS "  Blobwar — configuration résumée")
message(STATUS "  SFML source        : ${_BLOBWAR_SFML_SOURCE}")
message(STATUS "  SFML statique      : ${SFML_STATIC_LIBRARIES}")
message(STATUS "  SFML_DIR           : ${SFML_DIR}")
message(STATUS "  Build type         : ${CMAKE_BUILD_TYPE}")
message(STATUS "  Compilateur        : ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "----------------------------------------------------")
