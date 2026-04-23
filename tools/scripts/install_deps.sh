#!/usr/bin/env bash

set -euo pipefail

TARGET="${TARGET:-x86_64-elf}"
PREFIX="${PREFIX:-$HOME/cross}"
SRC_DIR="${SRC_DIR:-$HOME/src/toolchain}"
BINUTILS_VERSION="${BINUTILS_VERSION:-2.42}"
GCC_VERSION="${GCC_VERSION:-13.2.0}"
BUILD_JOBS="${BUILD_JOBS:-$(nproc)}"
INSTALL_SMOKE_DEPS=1
INSTALL_CROSS_TOOLCHAIN=1

usage() {
    cat <<EOF
Uso: ./install.sh [opcoes]

Opcoes:
  --skip-cross           instala apenas dependencias de build do host
  --skip-smoke           nao instala dependencias opcionais de smoke/QEMU
  --prefix DIR           define o prefixo da toolchain cruzada (padrao: $PREFIX)
  --src-dir DIR          define o diretorio de fontes/build da toolchain (padrao: $SRC_DIR)
  --target TARGET        define o target da toolchain (padrao: $TARGET)
  --binutils VERSION     define a versao do binutils (padrao: $BINUTILS_VERSION)
  --gcc VERSION          define a versao do GCC (padrao: $GCC_VERSION)
  --jobs N               define o numero de jobs do make (padrao: $BUILD_JOBS)
  -h, --help             mostra esta ajuda
EOF
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --skip-cross)
            INSTALL_CROSS_TOOLCHAIN=0
            ;;
        --skip-smoke)
            INSTALL_SMOKE_DEPS=0
            ;;
        --prefix)
            PREFIX="$2"
            shift
            ;;
        --src-dir)
            SRC_DIR="$2"
            shift
            ;;
        --target)
            TARGET="$2"
            shift
            ;;
        --binutils)
            BINUTILS_VERSION="$2"
            shift
            ;;
        --gcc)
            GCC_VERSION="$2"
            shift
            ;;
        --jobs)
            BUILD_JOBS="$2"
            shift
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            echo "[err] Opcao desconhecida: $1" >&2
            usage
            exit 1
            ;;
    esac
    shift
done

if [[ "${EUID}" -eq 0 ]]; then
    SUDO=""
else
    SUDO="sudo"
fi

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
PATH_EXPORT="export PATH=\"$PREFIX/bin:\$PATH\""

APT_PACKAGES=(
    build-essential
    binutils
    gcc
    make
    python3
    curl
    bison
    flex
    texinfo
    libgmp3-dev
    libmpc-dev
    libmpfr-dev
    libisl-dev
    nasm
    xorriso
    grub-common
    gnu-efi
)

SMOKE_PACKAGES=(
    qemu-system-x86
    ovmf
)

log() {
    printf '[info] %s\n' "$*"
}

ensure_path_export() {
    if [[ -f "$HOME/.bashrc" ]] && grep -Fqx "$PATH_EXPORT" "$HOME/.bashrc"; then
        return
    fi

    printf '\n%s\n' "$PATH_EXPORT" >> "$HOME/.bashrc"
    log "PATH persistido em ~/.bashrc para $PREFIX/bin"
}

download_if_missing() {
    local url="$1"
    local output="$2"

    if [[ -f "$output" ]]; then
        log "Arquivo ja presente: $output"
        return
    fi

    curl -L --fail --output "$output" "$url"
}

extract_if_missing() {
    local archive="$1"
    local dir="$2"

    if [[ -d "$dir" ]]; then
        log "Diretorio ja extraido: $dir"
        return
    fi

    tar -xf "$archive"
}

install_packages() {
    local packages=("${APT_PACKAGES[@]}")
    if [[ "$INSTALL_SMOKE_DEPS" -eq 1 ]]; then
        packages+=("${SMOKE_PACKAGES[@]}")
    fi

    log "Atualizando indice do apt"
    $SUDO apt-get update

    log "Instalando dependencias do host"
    $SUDO apt-get install -y "${packages[@]}"
}

build_binutils() {
    local build_dir="$SRC_DIR/build-binutils-$TARGET"
    local src_dir="$SRC_DIR/binutils-$BINUTILS_VERSION"
    local stamp="$build_dir/.installed"

    if [[ -f "$stamp" ]]; then
        log "Binutils $TARGET ja instalado em $PREFIX"
        return
    fi

    mkdir -p "$build_dir"
    pushd "$build_dir" >/dev/null
    "$src_dir/configure" \
        --target="$TARGET" \
        --prefix="$PREFIX" \
        --with-sysroot \
        --disable-nls \
        --disable-werror
    make -j"$BUILD_JOBS"
    make install
    popd >/dev/null

    touch "$stamp"
}

build_gcc() {
    local build_dir="$SRC_DIR/build-gcc-$TARGET"
    local src_dir="$SRC_DIR/gcc-$GCC_VERSION"
    local stamp="$build_dir/.installed"

    if [[ -f "$stamp" ]]; then
        log "GCC $TARGET ja instalado em $PREFIX"
        return
    fi

    mkdir -p "$build_dir"
    pushd "$build_dir" >/dev/null
    "$src_dir/configure" \
        --target="$TARGET" \
        --prefix="$PREFIX" \
        --disable-nls \
        --enable-languages=c \
        --without-headers
    make all-gcc -j"$BUILD_JOBS"
    make all-target-libgcc -j"$BUILD_JOBS"
    make install-gcc
    make install-target-libgcc
    popd >/dev/null

    touch "$stamp"
}

install_cross_toolchain() {
    mkdir -p "$SRC_DIR"
    pushd "$SRC_DIR" >/dev/null

    download_if_missing \
        "https://ftp.gnu.org/gnu/binutils/binutils-$BINUTILS_VERSION.tar.xz" \
        "binutils-$BINUTILS_VERSION.tar.xz"
    download_if_missing \
        "https://ftp.gnu.org/gnu/gcc/gcc-$GCC_VERSION/gcc-$GCC_VERSION.tar.xz" \
        "gcc-$GCC_VERSION.tar.xz"

    extract_if_missing "binutils-$BINUTILS_VERSION.tar.xz" "binutils-$BINUTILS_VERSION"
    extract_if_missing "gcc-$GCC_VERSION.tar.xz" "gcc-$GCC_VERSION"

    build_binutils
    build_gcc

    popd >/dev/null
}

verify_tool() {
    local tool="$1"
    if ! command -v "$tool" >/dev/null 2>&1; then
        printf '[err] Ferramenta ausente: %s\n' "$tool" >&2
        exit 1
    fi
}

main() {
    log "Projeto detectado em $PROJECT_ROOT"
    install_packages
    ensure_path_export

    export PATH="$PREFIX/bin:$PATH"

    if [[ "$INSTALL_CROSS_TOOLCHAIN" -eq 1 ]]; then
        install_cross_toolchain
    else
        log "Pulando instalacao da toolchain cruzada por solicitacao"
    fi

    verify_tool python3
    verify_tool xorriso
    verify_tool grub-mkrescue
    verify_tool x86_64-linux-gnu-gcc
    verify_tool x86_64-linux-gnu-ld
    verify_tool x86_64-linux-gnu-objcopy

    if [[ "$INSTALL_CROSS_TOOLCHAIN" -eq 1 ]]; then
        verify_tool "$TARGET-gcc"
        verify_tool "$TARGET-ld"
        verify_tool "$TARGET-objcopy"
    fi

    if [[ -f "$PROJECT_ROOT/tools/scripts/check_deps.py" ]]; then
        log "Validando dependencias do projeto"
        python3 "$PROJECT_ROOT/tools/scripts/check_deps.py"
    fi

    cat <<EOF

[ok] Ambiente preparado.

Comandos sugeridos:
  source ~/.bashrc
  make check-toolchain
  make all64
  make iso-uefi

EOF
}

main "$@"
