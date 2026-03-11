#!/usr/bin/env bash
# =============================================================================
#  install.sh — apex package manager installer
#
#  Detects your distro, installs build dependencies, compiles apex,
#  installs to /usr/local, sets up config and directory structure.
#
#  Usage:
#    sudo bash install.sh           # full install
#    sudo bash install.sh --prefix=/opt/apex  # custom prefix
#    bash install.sh --user         # user install to ~/.local
#    sudo bash install.sh --uninstall
# =============================================================================

set -euo pipefail

# ── Colours ───────────────────────────────────────────────────────────────────
R='\033[1;31m'; G='\033[1;32m'; Y='\033[1;33m'
C='\033[1;36m'; W='\033[1;37m'; D='\033[2m'; N='\033[0m'

info()  { echo -e "${C}::${N} $*"; }
ok()    { echo -e "${G}✓${N}  $*"; }
warn()  { echo -e "${Y}⚠${N}  $*"; }
die()   { echo -e "${R}✗${N}  $*" >&2; exit 1; }
hline() { printf "${D}%*s${N}\n" "$(tput cols 2>/dev/null || echo 60)" "" | tr ' ' '─'; }

# ── Defaults ──────────────────────────────────────────────────────────────────
PREFIX="/usr/local"
USER_INSTALL=false
UNINSTALL=false
JOBS=$(nproc 2>/dev/null || echo 4)
SRC="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# ── Argument parsing ──────────────────────────────────────────────────────────
for arg in "$@"; do
    case "$arg" in
        --prefix=*)  PREFIX="${arg#--prefix=}" ;;
        --user)      USER_INSTALL=true; PREFIX="$HOME/.local" ;;
        --uninstall) UNINSTALL=true ;;
        --jobs=*)    JOBS="${arg#--jobs=}" ;;
        --help|-h)
            echo ""
            echo "  Usage: [sudo] bash install.sh [options]"
            echo ""
            echo "  Options:"
            echo "    --prefix=<dir>   Install prefix (default: /usr/local)"
            echo "    --user           Install to ~/.local (no root needed)"
            echo "    --uninstall      Remove apex"
            echo "    --jobs=<n>       Parallel build jobs (default: nproc)"
            echo "    --help           This message"
            echo ""
            exit 0
            ;;
        *) warn "Unknown option: $arg" ;;
    esac
done

# ── Banner ────────────────────────────────────────────────────────────────────
echo ""
echo -e "${C}"
echo "   ██████╗ ██████╗ ███████╗██╗  ██╗"
echo "  ██╔══██╗██╔══██╗██╔════╝╚██╗██╔╝"
echo "  ███████║██████╔╝█████╗   ╚███╔╝ "
echo "  ██╔══██║██╔═══╝ ██╔══╝   ██╔██╗ "
echo "  ██║  ██║██║     ███████╗██╔╝ ██╗"
echo "  ╚═╝  ╚═╝╚═╝     ╚══════╝╚═╝  ╚═╝"
echo -e "${N}"
echo -e "${D}  Apex Package Manager — Installer${N}"
hline
echo ""

# ── Uninstall ─────────────────────────────────────────────────────────────────
if $UNINSTALL; then
    info "Removing apex..."
    rm -f  "$PREFIX/bin/apex" "$PREFIX/bin/apexbuild"
    rm -f  "$PREFIX/share/man/man1/apex.1"
    rm -f  "$PREFIX/share/bash-completion/completions/apex"
    rm -f  "$PREFIX/share/zsh/site-functions/_apex"
    ok "apex removed from $PREFIX"
    echo ""
    echo -e "  ${Y}Config and database kept:${N}"
    echo "    /etc/apex/     — run: sudo rm -rf /etc/apex"
    echo "    /var/lib/apex/ — run: sudo rm -rf /var/lib/apex"
    echo "    /var/cache/apex/ — run: sudo rm -rf /var/cache/apex"
    echo ""
    exit 0
fi

# ── Root check ────────────────────────────────────────────────────────────────
if ! $USER_INSTALL && [[ $EUID -ne 0 ]]; then
    die "Root required. Use: sudo bash install.sh\n  Or for user install: bash install.sh --user"
fi

# ── Detect distro ─────────────────────────────────────────────────────────────
detect_distro() {
    if   [[ -f /etc/arch-release ]];   then echo "arch"
    elif [[ -f /etc/debian_version ]]; then echo "debian"
    elif [[ -f /etc/fedora-release ]]; then echo "fedora"
    elif [[ -f /etc/redhat-release ]]; then echo "rhel"
    elif [[ -f /etc/opensuse-release ]] || [[ -f /etc/SUSE-brand ]]; then echo "opensuse"
    elif [[ -f /etc/alpine-release ]]; then echo "alpine"
    elif command -v brew &>/dev/null;  then echo "macos"
    else echo "unknown"
    fi
}

DISTRO=$(detect_distro)
info "Detected distro: ${W}${DISTRO}${N}"

# ── Install build dependencies ────────────────────────────────────────────────
install_deps() {
    info "Installing build dependencies..."
    case "$DISTRO" in
    arch)
        pacman -S --needed --noconfirm \
            cmake gcc make \
            curl libarchive zstd \
            libgcrypt gpgme \
            pkg-config \
            bash-completion
        ;;
    debian)
        apt-get update -qq
        apt-get install -y --no-install-recommends \
            cmake gcc g++ make \
            libcurl4-openssl-dev \
            libarchive-dev \
            libzstd-dev \
            libgcrypt20-dev \
            libgpgme-dev \
            pkg-config \
            bash-completion
        ;;
    fedora|rhel)
        dnf install -y \
            cmake gcc gcc-c++ make \
            libcurl-devel \
            libarchive-devel \
            libzstd-devel \
            libgcrypt-devel \
            gpgme-devel \
            pkgconf \
            bash-completion
        ;;
    opensuse)
        zypper install -y \
            cmake gcc gcc-c++ make \
            libcurl-devel \
            libarchive-devel \
            libzstd-devel \
            libgcrypt-devel \
            gpgme-devel \
            pkg-config \
            bash-completion
        ;;
    alpine)
        apk add --no-cache \
            cmake gcc g++ make \
            curl-dev \
            libarchive-dev \
            zstd-dev \
            libgcrypt-dev \
            gpgme-dev \
            pkgconf \
            bash-completion
        ;;
    macos)
        brew install cmake curl libarchive zstd libgcrypt gpgme pkg-config
        ;;
    *)
        warn "Unknown distro — assuming build deps are already installed."
        warn "You need: cmake gcc libcurl libarchive libzstd libgcrypt"
        ;;
    esac
    ok "Build dependencies ready."
}

# Ask before installing deps if interactive
if [[ -t 0 ]]; then
    echo -e "  ${W}Build dependencies will be installed for: ${DISTRO}${N}"
    read -rp "  Install build dependencies? [Y/n] " ans
    [[ "$ans" == "" || "$ans" == "Y" || "$ans" == "y" ]] && install_deps || true
else
    install_deps
fi

# ── Verify source files ───────────────────────────────────────────────────────
hline
info "Checking source tree..."

REQUIRED_FILES=(
    "CMakeLists.txt"
    "src/ui/main.cpp"
    "src/util/util.c"
    "src/core/version.c"
    "src/solver/solver.c"
    "include/apex/apex.h"
    "include/apex/util.h"
    "include/apex/version.h"
    "include/apex/solver.h"
    "include/apex/db.h"
    "include/apex/net.h"
    "include/apex/pkg.h"
    "include/apex/crypto.h"
)

missing=0
for f in "${REQUIRED_FILES[@]}"; do
    if [[ ! -f "$SRC/$f" ]]; then
        warn "Missing: $f"
        missing=1
    fi
done
[[ $missing -eq 1 ]] && die "Source tree incomplete. Re-download apex."
ok "Source tree OK."

# ── Build ─────────────────────────────────────────────────────────────────────
hline
info "Building apex (jobs: ${JOBS})..."

BUILD_DIR="$SRC/build"
rm -rf "$BUILD_DIR"
mkdir -p "$BUILD_DIR"

cmake "$SRC" \
    -B "$BUILD_DIR" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="$PREFIX" \
    -DCMAKE_C_FLAGS="-O2" \
    -DCMAKE_CXX_FLAGS="-O2" \
    2>&1 | grep -v "^--" || true

cmake --build "$BUILD_DIR" -j"$JOBS"
ok "Build complete."

# ── Run tests ─────────────────────────────────────────────────────────────────
hline
info "Running tests..."
cd "$BUILD_DIR"
if ctest --output-on-failure -j"$JOBS" 2>&1; then
    ok "All tests passed."
else
    warn "Some tests failed — install will continue."
fi
cd "$SRC"

# ── Install binaries ──────────────────────────────────────────────────────────
hline
info "Installing to ${PREFIX}..."

cmake --install "$BUILD_DIR"

# Create necessary directories
mkdir -p /etc/apex/repos.d
mkdir -p /etc/apex/hooks.d
mkdir -p /etc/apex/scripts.d
mkdir -p /var/lib/apex/local
mkdir -p /var/lib/apex/sync
mkdir -p /var/cache/apex/pkg
mkdir -p /var/log

# Config file (only if not present)
if [[ ! -f /etc/apex/apex.conf ]]; then
    info "Writing default config: /etc/apex/apex.conf"
    cat > /etc/apex/apex.conf << 'CONF_EOF'
# /etc/apex/apex.conf
# apex package manager configuration

[options]
# Root directory for installations (leave blank for /)
RootDir     =

# Database path
DBPath      = /var/lib/apex

# Package cache
CacheDir    = /var/cache/apex/pkg

# Log file
LogFile     = /var/log/apex.log

# Verify GPG signatures on packages and repo indexes
VerifySig   = yes

# Number of parallel download connections
ParallelDl  = 4

# Download timeout in seconds
DlTimeout   = 30

# Use binary delta upgrades when available (saves bandwidth)
DeltaUpgrade = yes

# Colour output
Color       = yes

# Show download progress bars
Progress    = yes

# Check available disk space before installing
CheckSpace  = yes

# Packages that should never be upgraded (space-separated)
# IgnoreUpgrade =

# Config files that should never be overwritten
# NoUpgrade =

# Files that should never be extracted from packages
# NoExtract =

[repos]
# Repositories are configured in /etc/apex/repos.d/*.repo
# See /etc/apex/repos.d/example.repo for format
CONF_EOF
    ok "Config written."
fi

# Example repo file
if [[ ! -f /etc/apex/repos.d/example.repo ]]; then
    cat > /etc/apex/repos.d/example.repo << 'REPO_EOF'
# Example repository configuration
# Copy this file and edit it to add a real repository.
#
# [reponame]
# Url     = https://repo.example.com/stable/$arch
# GpgKey  = ABCDEF1234567890
# Enabled = yes
# Verify  = yes

[core]
Url     = https://p1rater.github.io/apex-repo/core/$arch
Enabled = yes
Verify  = no

[extra]
Url     = https://p1rater.github.io/apex-repo/extra/$arch
Enabled = no
Verify  = no
REPO_EOF
    ok "Repo configuration set to p1rater.github.io"
fi

# Permissions
chmod 755 /etc/apex /etc/apex/repos.d /etc/apex/hooks.d
chmod 644 /etc/apex/apex.conf
chmod 700 /var/lib/apex

ok "Directories and config ready."

# ── Shell completions ─────────────────────────────────────────────────────────
hline
info "Installing shell completions..."

BASH_COMP_DIR="$PREFIX/share/bash-completion/completions"
ZSH_COMP_DIR="$PREFIX/share/zsh/site-functions"

mkdir -p "$BASH_COMP_DIR" "$ZSH_COMP_DIR"

[[ -f "$SRC/completions/apex.bash" ]] && \
    install -m644 "$SRC/completions/apex.bash" \
                  "$BASH_COMP_DIR/apex" && \
    ok "Bash completion installed."

[[ -f "$SRC/completions/apex.zsh" ]] && \
    install -m644 "$SRC/completions/apex.zsh" \
                  "$ZSH_COMP_DIR/_apex" && \
    ok "Zsh completion installed."

# ── Man page ──────────────────────────────────────────────────────────────────
if [[ -f "$SRC/man/apex.1" ]]; then
    install -Dm644 "$SRC/man/apex.1" \
                   "$PREFIX/share/man/man1/apex.1"
    ok "Man page installed."
fi

# ── PATH reminder for user installs ───────────────────────────────────────────
if $USER_INSTALL; then
    echo ""
    warn "User install — make sure ~/.local/bin is in your PATH:"
    echo '  echo '"'"'export PATH="$HOME/.local/bin:$PATH"'"'"' >> ~/.bashrc'
    echo '  source ~/.bashrc'
fi

# ── Clean up build dir ────────────────────────────────────────────────────────
rm -rf "$BUILD_DIR"

# ── Done ──────────────────────────────────────────────────────────────────────
hline
echo ""
ok "apex $(apex --version 2>/dev/null || echo 'installed') successfully!"
echo ""
echo -e "  ${W}Quick start:${N}"
echo "    apex --help"
echo "    apex --version"
echo "    apex search firefox"
echo "    sudo apex sync"
echo "    sudo apex install <package>"
echo "    sudo apex upgrade"
echo "    apex list"
echo "    apex info <package>"
echo ""
echo -e "  ${W}Config:${N}  /etc/apex/apex.conf"
echo -e "  ${W}Repos:${N}   /etc/apex/repos.d/*.repo"
echo -e "  ${W}DB:${N}      /var/lib/apex/"
echo -e "  ${W}Cache:${N}   /var/cache/apex/pkg/"
echo -e "  ${W}Log:${N}     /var/log/apex.log"
echo ""
echo -e "  ${D}Source: https://github.com/p1rater/apex${N}"
echo ""
