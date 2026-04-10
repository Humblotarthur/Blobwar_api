#!/usr/bin/env bash
# setupenv.sh — Configure l'environnement de développement Blob_war
#
# Ce script :
#   1. Installe les dépendances système (apt / brew)
#   2. Compile SFML 3.0.0 en statique dans lib/SFML/  (si absent)
#   3. Installe les dépendances Python (torch, numpy)
#   4. Génère .env avec les variables d'environnement du projet
#
# Usage :
#   bash setupenv.sh          # setup complet
#   bash setupenv.sh --python # Python uniquement (skip SFML)
#   bash setupenv.sh --sfml   # SFML uniquement
#   bash setupenv.sh --env    # Génère .env uniquement
#
# Activer les variables après setup :
#   source .env

set -euo pipefail

# ── Couleurs ──────────────────────────────────────────────────────────────────

BOLD='\033[1m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
NC='\033[0m'

info()    { echo -e "${GREEN}[INFO]${NC} $*"; }
warn()    { echo -e "${YELLOW}[WARN]${NC} $*"; }
error()   { echo -e "${RED}[ERROR]${NC} $*" >&2; exit 1; }
step()    { echo -e "\n${BOLD}── $* ──────────────────────────────────${NC}"; }

# ── Chemins ───────────────────────────────────────────────────────────────────

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
LIB_DIR="$PROJECT_ROOT/lib"
SFML_SRC="$LIB_DIR/SFML-src"
SFML_BUILD="$LIB_DIR/SFML-build"
SFML_INSTALL="$LIB_DIR/SFML"
SFML_VERSION="3.0.0"
SFML_REPO="https://github.com/SFML/SFML.git"

ENV_FILE="$PROJECT_ROOT/.env"

# ── Arguments ─────────────────────────────────────────────────────────────────

DO_SYSTEM=true
DO_SFML=true
DO_PYTHON=true
DO_ENV=true

for arg in "$@"; do
    case "$arg" in
        --python) DO_SYSTEM=false; DO_SFML=false ;;
        --sfml)   DO_SYSTEM=true;  DO_PYTHON=false ;;
        --env)    DO_SYSTEM=false; DO_SFML=false; DO_PYTHON=false ;;
        --help|-h)
            echo "Usage: bash setupenv.sh [--python|--sfml|--env]"
            exit 0 ;;
    esac
done

# ── Détection OS ──────────────────────────────────────────────────────────────

detect_os() {
    if [[ "$OSTYPE" == "darwin"* ]]; then
        echo "macos"
    elif grep -qi microsoft /proc/version 2>/dev/null; then
        echo "wsl"
    elif [[ -f /etc/debian_version ]]; then
        echo "debian"
    elif [[ -f /etc/redhat-release ]]; then
        echo "redhat"
    else
        echo "linux"
    fi
}

OS=$(detect_os)
info "Système détecté : $OS"

# ── 1. Dépendances système ────────────────────────────────────────────────────

if $DO_SYSTEM; then
    step "Dépendances système"

    if [[ "$OS" == "macos" ]]; then
        if ! command -v brew &>/dev/null; then
            error "Homebrew introuvable. Installer depuis https://brew.sh"
        fi
        info "Installation via Homebrew..."
        brew install cmake git tbb
        # SFML dépendances système
        brew install freetype

    elif [[ "$OS" == "debian" || "$OS" == "wsl" ]]; then
        info "Installation via apt..."
        sudo apt-get update -qq
        sudo apt-get install -y \
            build-essential \
            cmake \
            git \
            pkg-config \
            libtbb-dev \
            libfreetype-dev \
            libx11-dev \
            libxrandr-dev \
            libxcursor-dev \
            libxi-dev \
            libudev-dev \
            libgl-dev \
            libflac-dev \
            libogg-dev \
            libvorbis-dev \
            python3 \
            python3-pip \
            python3-venv

    elif [[ "$OS" == "redhat" ]]; then
        info "Installation via dnf..."
        sudo dnf install -y \
            cmake gcc-c++ git \
            tbb-devel \
            freetype-devel \
            libX11-devel libXrandr-devel libXcursor-devel libXi-devel \
            systemd-devel mesa-libGL-devel \
            flac-devel libogg-devel libvorbis-devel \
            python3 python3-pip

    else
        warn "OS non reconnu — installation manuelle des dépendances requise."
        warn "Dépendances nécessaires : cmake g++ git libtbb-dev libfreetype-dev"
        warn "  libx11-dev libxrandr-dev libxcursor-dev libudev-dev libgl-dev"
    fi

    # Vérification des outils essentiels
    for tool in cmake g++ git python3; do
        if ! command -v "$tool" &>/dev/null; then
            error "$tool introuvable après installation."
        fi
    done
    info "Outils système OK (cmake $(cmake --version | head -1 | grep -oE '[0-9]+\.[0-9]+\.[0-9]+'), g++ $(g++ --version | head -1 | grep -oE '[0-9]+\.[0-9]+\.[0-9]+' | head -1))"
fi

# ── 2. SFML 3.0.0 ─────────────────────────────────────────────────────────────

if $DO_SFML; then
    step "SFML $SFML_VERSION"

    if [[ -d "$SFML_INSTALL/lib/cmake/SFML" ]]; then
        info "SFML déjà installé dans lib/SFML/ — skip."
    else
        mkdir -p "$LIB_DIR"

        # Cloner les sources si absentes
        if [[ ! -d "$SFML_SRC/CMakeLists.txt" && ! -d "$SFML_SRC/cmake" ]]; then
            info "Clonage de SFML $SFML_VERSION..."
            git clone --branch "$SFML_VERSION" --depth 1 "$SFML_REPO" "$SFML_SRC"
        else
            info "Sources SFML déjà présentes dans lib/SFML-src/"
        fi

        info "Configuration CMake de SFML..."
        cmake -S "$SFML_SRC" -B "$SFML_BUILD" \
            -DCMAKE_BUILD_TYPE=Release \
            -DBUILD_SHARED_LIBS=OFF \
            -DSFML_BUILD_AUDIO=OFF \
            -DSFML_BUILD_NETWORK=OFF \
            -DSFML_BUILD_EXAMPLES=OFF \
            -DSFML_BUILD_TEST_SUITE=OFF \
            -DCMAKE_INSTALL_PREFIX="$SFML_INSTALL" \
            -DCMAKE_POSITION_INDEPENDENT_CODE=ON

        info "Compilation de SFML ($(nproc) cœurs)..."
        cmake --build "$SFML_BUILD" --config Release -j"$(nproc)"

        info "Installation de SFML dans lib/SFML/..."
        cmake --install "$SFML_BUILD"

        info "SFML $SFML_VERSION installé."
    fi
fi

# ── 3. Dépendances Python ─────────────────────────────────────────────────────

if $DO_PYTHON; then
    step "Dépendances Python"

    REQUIREMENTS="$PROJECT_ROOT/training/requirements.txt"

    if [[ ! -f "$REQUIREMENTS" ]]; then
        error "Fichier introuvable : $REQUIREMENTS"
    fi

    # Utiliser un venv si disponible, sinon pip global
    VENV_DIR="$PROJECT_ROOT/.venv"
    if [[ ! -d "$VENV_DIR" ]]; then
        info "Création du virtualenv Python dans .venv/..."
        python3 -m venv "$VENV_DIR"
    fi

    info "Installation des packages Python..."
    "$VENV_DIR/bin/pip" install --upgrade pip -q
    "$VENV_DIR/bin/pip" install -r "$REQUIREMENTS"

    PYTHON_BIN="$VENV_DIR/bin/python"
    TORCH_VERSION=$("$PYTHON_BIN" -c "import torch; print(torch.__version__)" 2>/dev/null || echo "non installé")
    info "PyTorch : $TORCH_VERSION"
fi

# ── 4. Fichier .env ───────────────────────────────────────────────────────────

if $DO_ENV; then
    step "Génération de .env"

    PYTHON_BIN_PATH="${VENV_DIR:-$PROJECT_ROOT/.venv}/bin"

    cat > "$ENV_FILE" << EOF
# Blob_war — variables d'environnement
# Généré par setupenv.sh le $(date '+%Y-%m-%d')
# Usage : source .env

# Racine du projet
export BLOB_WAR_ROOT="$PROJECT_ROOT"

# Python (virtualenv)
export PATH="$PYTHON_BIN_PATH:\$PATH"
export PYTHONPATH="$PROJECT_ROOT/training:\$PYTHONPATH"

# CMake — hint SFML local (utilisé si CMAKE_PREFIX_PATH non défini)
export CMAKE_PREFIX_PATH="$SFML_INSTALL:\${CMAKE_PREFIX_PATH:-}"

# Threads TBB
export TBB_NUM_THREADS="\${TBB_NUM_THREADS:-$(nproc)}"
EOF

    info ".env généré → $ENV_FILE"
fi

# ── Résumé ────────────────────────────────────────────────────────────────────

echo ""
echo -e "${BOLD}══════════════════════════════════════════════${NC}"
echo -e "${GREEN}  Setup terminé${NC}"
echo -e "${BOLD}══════════════════════════════════════════════${NC}"
echo ""
echo "  Activer l'environnement :"
echo -e "    ${BOLD}source .env${NC}"
echo ""
echo "  Compiler le projet :"
echo -e "    ${BOLD}mkdir -p build && cd build && cmake .. && make -j\$(nproc)${NC}"
echo ""
echo "  Générer les datasets :"
echo -e "    ${BOLD}cd build && ./data_gen --all${NC}"
echo ""
echo "  Entraîner le CNN :"
echo -e "    ${BOLD}cd training && python train.py${NC}"
echo ""
