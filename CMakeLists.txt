cmake_minimum_required(VERSION 3.16)
project(Blobwar CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# SFML 3.0 installé localement dans lib/SFML (compilé en statique)
set(SFML_DIR "${CMAKE_SOURCE_DIR}/lib/SFML/lib/cmake/SFML")
set(SFML_STATIC_LIBRARIES TRUE)
find_package(SFML 3 REQUIRED COMPONENTS Graphics Window System)
find_package(TBB REQUIRED)

# --- Moteur (aucune dépendance externe) ---
add_library(blobwar_engine STATIC
    src/engine/Board.cpp
    src/engine/Rules.cpp
    src/engine/MoveGen.cpp
)
target_include_directories(blobwar_engine PUBLIC src/engine)

# --- Evaluation partagée ---
add_library(blobwar_eval STATIC
    src/ai/Eval/Eval.cpp
)
target_include_directories(blobwar_eval PUBLIC src/ai/Eval)
target_link_libraries(blobwar_eval PUBLIC blobwar_engine)

# --- IA (dépend du moteur, pas de SFML) ---
add_library(blobwar_ai STATIC
    src/ai/Random/RandomAI.cpp
    src/ai/AlphaBeta/AlphaBetaAI.cpp
    src/ai/Negamax/NegamaxAI.cpp
    src/ai/NegamaxParallel/NegamaxParallelAI.cpp
    src/ai/AIProcess.cpp
)
target_include_directories(blobwar_ai PUBLIC
    src/ai
    src/ai/Eval
    src/ai/Random
    src/ai/AlphaBeta
    src/ai/Negamax
    src/ai/NegamaxParallel
)
target_link_libraries(blobwar_ai PUBLIC blobwar_eval TBB::tbb)

# --- Interface graphique ---
add_library(blobwar_ui STATIC
    src/ui/Renderer.cpp
    src/ui/App.cpp
)
target_include_directories(blobwar_ui PUBLIC src/ui)
target_link_libraries(blobwar_ui PUBLIC blobwar_engine blobwar_ai SFML::Graphics SFML::Window SFML::System)

# --- Exécutable principal ---
add_executable(blobwar src/main.cpp)
target_include_directories(blobwar PRIVATE src/ui)
target_link_libraries(blobwar PRIVATE blobwar_ui blobwar_ai)

# --- Processus fils IA (binaire séparé) ---
add_executable(ai_stub ai_stub/main.cpp)
target_link_libraries(ai_stub PRIVATE blobwar_ai)
