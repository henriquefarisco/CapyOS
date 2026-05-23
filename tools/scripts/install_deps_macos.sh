#!/usr/bin/env bash

set -euo pipefail

TARGET="${TARGET:-x86_64-elf}"
PREFIX="${PREFIX:-$HOME/cross}"
SRC_DIR="${SRC_DIR:-$HOME/src/toolchain}"
BINUTILS_VERSION="${BINUTILS_VERSION:-2.42}"
GCC_VERSION="${GCC_VERSION:-13.2.0}"
GMP_VERSION="${GMP_VERSION:-6.3.0}"
MPFR_VERSION="${MPFR_VERSION:-4.2.1}"
MPC_VERSION="${MPC_VERSION:-1.3.1}"
ISL_VERSION="${ISL_VERSION:-0.26}"
BUILD_JOBS="${BUILD_JOBS:-$(sysctl -n hw.ncpu 2>/dev/null || echo 4)}"
INSTALL_HOST_DEPS=0
INSTALL_SMOKE_DEPS=0
INSTALL_CROSS_TOOLCHAIN=0
INSTALL_HOMEBREW=0
INSTALL_PATH_EXPORT=1
SKIP_BREW_UPDATE="${SKIP_BREW_UPDATE:-0}"
DRY_RUN=0
BREW_BIN="${BREW_BIN:-}"
MAKE_BIN=""
BREW_PREFIX=""
LOCAL_DEPS_DIR="${LOCAL_DEPS_DIR:-}"
ALLOW_ROOT_INSTALL="${ALLOW_ROOT_INSTALL:-0}"

usage() {
    cat <<EOF
Uso: ./install-macos.sh [opcoes]

Opcoes:
  --with-homebrew-deps   instala dependencias Homebrew minimas para ISO local
  --with-cross           instala dependencias e compila a toolchain $TARGET
  --with-smoke           instala dependencias opcionais de smoke/QEMU
  --skip-host-deps       compatibilidade: mantem deps Homebrew desativadas
  --skip-cross           compatibilidade: mantem toolchain cruzada desativada
  --skip-smoke           compatibilidade: mantem smoke/QEMU desativado
  --skip-brew-update     pula brew update antes de instalar pacotes
  --no-homebrew-install  falha se Homebrew nao existir em vez de instala-lo
  --prefix DIR           define o prefixo da toolchain cruzada (padrao: $PREFIX)
  --src-dir DIR          define o diretorio de fontes/build da toolchain (padrao: $SRC_DIR)
  --target TARGET        define o target da toolchain (padrao: $TARGET)
  --binutils VERSION     define a versao do binutils (padrao: $BINUTILS_VERSION)
  --gcc VERSION          define a versao do GCC (padrao: $GCC_VERSION)
  --local-deps DIR       usa arquivos locais aprovados em DIR e nao baixa fontes
  --no-path-export       nao altera arquivos de shell do usuario
  --jobs N               define o numero de jobs do make (padrao: $BUILD_JOBS)
  --dry-run              mostra acoes sem instala-las
  -h, --help             mostra esta ajuda
EOF
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --with-homebrew-deps)
            INSTALL_HOST_DEPS=1
            INSTALL_HOMEBREW=1
            ;;
        --with-cross)
            INSTALL_CROSS_TOOLCHAIN=1
            INSTALL_HOMEBREW=1
            ;;
        --with-smoke)
            INSTALL_SMOKE_DEPS=1
            INSTALL_HOMEBREW=1
            ;;
        --skip-host-deps)
            INSTALL_HOST_DEPS=0
            ;;
        --skip-cross)
            INSTALL_CROSS_TOOLCHAIN=0
            ;;
        --skip-smoke)
            INSTALL_SMOKE_DEPS=0
            ;;
        --skip-brew-update)
            SKIP_BREW_UPDATE=1
            ;;
        --no-homebrew-install)
            INSTALL_HOMEBREW=0
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
        --local-deps)
            LOCAL_DEPS_DIR="$2"
            shift
            ;;
        --no-path-export)
            INSTALL_PATH_EXPORT=0
            ;;
        --jobs)
            BUILD_JOBS="$2"
            shift
            ;;
        --dry-run)
            DRY_RUN=1
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

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
PATH_EXPORT="export PATH=\"$PREFIX/bin:\$PATH\""

HOST_PACKAGES=(
    nasm
    xorriso
    mtools
)

CROSS_PACKAGES=(
    gmp
    mpfr
    libmpc
    isl
    bison
    flex
    texinfo
    pkg-config
)

SMOKE_PACKAGES=(
    qemu
)

log() {
    printf '[info] %s\n' "$*"
}

warn() {
    printf '[warn] %s\n' "$*" >&2
}

die() {
    printf '[err] %s\n' "$*" >&2
    exit 1
}

run() {
    if [[ "$DRY_RUN" -eq 1 ]]; then
        printf '[dry-run]'
        printf ' %q' "$@"
        printf '\n'
        return
    fi
    "$@"
}

ensure_macos() {
    if [[ "$(uname -s)" != "Darwin" ]]; then
        die "Este instalador e exclusivo para macOS. Use ./install.sh em Linux/WSL."
    fi
}

ensure_not_root() {
    if [[ "${EUID}" -eq 0 && "$ALLOW_ROOT_INSTALL" != "1" ]]; then
        die "Nao execute install-macos.sh com sudo/root; rode como usuario normal para evitar artefatos root-owned no workspace."
    fi
}

ensure_xcode_tools() {
    if xcode-select -p >/dev/null 2>&1; then
        return
    fi
    warn "Xcode Command Line Tools nao encontrado."
    if [[ -n "$LOCAL_DEPS_DIR" ]]; then
        die "Modo local nao instala Xcode Command Line Tools automaticamente."
    fi
    if [[ "$DRY_RUN" -eq 1 ]]; then
        log "Instalaria Xcode Command Line Tools com xcode-select --install"
        return
    fi
    xcode-select --install || true
    die "Conclua a instalacao do Xcode Command Line Tools e execute este script novamente."
}

detect_brew_bin() {
    if [[ -n "$BREW_BIN" && -x "$BREW_BIN" ]]; then
        return
    fi
    if command -v brew >/dev/null 2>&1; then
        BREW_BIN="$(command -v brew)"
        return
    fi
    if [[ -x /opt/homebrew/bin/brew ]]; then
        BREW_BIN="/opt/homebrew/bin/brew"
        return
    fi
    if [[ -x /usr/local/bin/brew ]]; then
        BREW_BIN="/usr/local/bin/brew"
        return
    fi
}

ensure_homebrew() {
    detect_brew_bin || true
    if [[ -n "$BREW_BIN" ]]; then
        return
    fi
    if [[ "$INSTALL_HOMEBREW" -ne 1 ]]; then
        die "Homebrew nao encontrado. Instale Homebrew ou remova --no-homebrew-install."
    fi
    if [[ "$DRY_RUN" -eq 1 ]]; then
        log "Instalaria Homebrew pelo instalador oficial"
        BREW_BIN="/opt/homebrew/bin/brew"
        return
    fi
    log "Instalando Homebrew pelo instalador oficial"
    NONINTERACTIVE=1 /bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
    detect_brew_bin || die "Homebrew instalado, mas brew nao foi encontrado no PATH esperado."
}

load_homebrew_env() {
    if [[ "$DRY_RUN" -eq 1 && ! -x "$BREW_BIN" ]]; then
        BREW_PREFIX="${BREW_PREFIX:-/opt/homebrew}"
        return
    fi
    eval "$("$BREW_BIN" shellenv)"
    BREW_PREFIX="$("$BREW_BIN" --prefix)"
}

install_packages() {
    local packages=()
    if [[ "$INSTALL_HOST_DEPS" -eq 1 ]]; then
        packages+=("${HOST_PACKAGES[@]}")
    fi
    if [[ "$INSTALL_CROSS_TOOLCHAIN" -eq 1 ]]; then
        packages+=("${CROSS_PACKAGES[@]}")
    fi
    if [[ "$INSTALL_SMOKE_DEPS" -eq 1 ]]; then
        packages+=("${SMOKE_PACKAGES[@]}")
    fi
    if [[ "${#packages[@]}" -eq 0 ]]; then
        log "Sem pacotes Homebrew para instalar"
        return
    fi
    if [[ "$SKIP_BREW_UPDATE" -eq 1 ]]; then
        log "Pulando brew update por solicitacao"
    else
        log "Atualizando Homebrew"
        run "$BREW_BIN" update
    fi
    log "Instalando dependencias Homebrew"
    run "$BREW_BIN" install "${packages[@]}"
}

needs_homebrew() {
    [[ -z "$LOCAL_DEPS_DIR" &&
       ( "$INSTALL_HOST_DEPS" -eq 1 ||
         "$INSTALL_CROSS_TOOLCHAIN" -eq 1 ||
         "$INSTALL_SMOKE_DEPS" -eq 1 ) ]]
}

select_make() {
    if command -v gmake >/dev/null 2>&1; then
        MAKE_BIN="$(command -v gmake)"
    else
        MAKE_BIN="$(command -v make)"
    fi
}

ensure_path_export() {
    local shell_files=("$HOME/.zprofile" "$HOME/.zshrc")
    if [[ -f "$HOME/.bash_profile" ]]; then
        shell_files+=("$HOME/.bash_profile")
    fi
    for shell_file in "${shell_files[@]}"; do
        if [[ -f "$shell_file" ]] && grep -Fqx "$PATH_EXPORT" "$shell_file"; then
            continue
        fi
        if [[ "$DRY_RUN" -eq 1 ]]; then
            log "Adicionaria PATH da toolchain em $shell_file"
        else
            printf '\n%s\n' "$PATH_EXPORT" >> "$shell_file"
            log "PATH persistido em $shell_file para $PREFIX/bin"
        fi
    done
}

download_if_missing() {
    local url="$1"
    local output="$2"
    if [[ -f "$output" ]]; then
        log "Arquivo ja presente: $output"
        return
    fi
    if [[ -n "$LOCAL_DEPS_DIR" ]]; then
        local local_file="$LOCAL_DEPS_DIR/$output"
        if [[ ! -f "$local_file" ]]; then
            die "Arquivo local ausente: $local_file"
        fi
        run cp "$local_file" "$output"
        return
    fi
    run curl -L --fail --output "$output" "$url"
}

extract_if_missing() {
    local archive="$1"
    local dir="$2"
    if [[ -d "$dir" ]]; then
        log "Diretorio ja extraido: $dir"
        return
    fi
    run tar -xf "$archive"
}

build_binutils() {
    local build_dir="$SRC_DIR/build-binutils-$TARGET"
    local src_dir="$SRC_DIR/binutils-$BINUTILS_VERSION"
    local stamp="$build_dir/.installed"
    if [[ -f "$stamp" ]]; then
        log "Binutils $TARGET ja instalado em $PREFIX"
        return
    fi
    run mkdir -p "$build_dir"
    if [[ "$DRY_RUN" -eq 1 ]]; then
        log "Configuraria e instalaria binutils $BINUTILS_VERSION para $TARGET"
        return
    fi
    pushd "$build_dir" >/dev/null
    local configure_args=(
        --target="$TARGET" \
        --prefix="$PREFIX" \
        --with-sysroot \
        --disable-nls \
        --disable-werror
    )
    if [[ -n "$LOCAL_DEPS_DIR" && "$(uname -s)" == "Darwin" ]]; then
        configure_args+=(--with-system-zlib)
    fi
    "$src_dir/configure" "${configure_args[@]}"
    "$MAKE_BIN" -j"$BUILD_JOBS"
    "$MAKE_BIN" install
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
    run mkdir -p "$build_dir"
    if [[ "$DRY_RUN" -eq 1 ]]; then
        log "Configuraria e instalaria GCC $GCC_VERSION para $TARGET"
        return
    fi
    pushd "$build_dir" >/dev/null
    local configure_args=(
        --target="$TARGET" \
        --prefix="$PREFIX" \
        --disable-nls \
        --disable-multilib \
        --enable-languages=c \
        --without-headers
    )
    if [[ -z "$LOCAL_DEPS_DIR" ]]; then
        configure_args+=(
            --with-gmp="$BREW_PREFIX/opt/gmp"
            --with-mpfr="$BREW_PREFIX/opt/mpfr"
            --with-mpc="$BREW_PREFIX/opt/libmpc"
            --with-isl="$BREW_PREFIX/opt/isl"
        )
    elif [[ "$(uname -s)" == "Darwin" ]]; then
        configure_args+=(--without-isl --with-system-zlib)
    fi
    "$src_dir/configure" "${configure_args[@]}"
    "$MAKE_BIN" all-gcc -j"$BUILD_JOBS"
    "$MAKE_BIN" all-target-libgcc -j"$BUILD_JOBS"
    "$MAKE_BIN" install-gcc
    "$MAKE_BIN" install-target-libgcc
    popd >/dev/null
    touch "$stamp"
}

prepare_gcc_prereq() {
    local src_dir="$SRC_DIR/gcc-$GCC_VERSION"
    local dep_dir="$1"
    local link_name="$2"
    if [[ -e "$src_dir/$link_name" ]]; then
        return
    fi
    run ln -s "../$dep_dir" "$src_dir/$link_name"
}

prepare_local_gcc_prereqs() {
    download_if_missing "https://ftp.gnu.org/gnu/gmp/gmp-$GMP_VERSION.tar.xz" "gmp-$GMP_VERSION.tar.xz"
    download_if_missing "https://www.mpfr.org/mpfr-$MPFR_VERSION/mpfr-$MPFR_VERSION.tar.xz" "mpfr-$MPFR_VERSION.tar.xz"
    download_if_missing "https://ftp.gnu.org/gnu/mpc/mpc-$MPC_VERSION.tar.gz" "mpc-$MPC_VERSION.tar.gz"
    extract_if_missing "gmp-$GMP_VERSION.tar.xz" "gmp-$GMP_VERSION"
    extract_if_missing "mpfr-$MPFR_VERSION.tar.xz" "mpfr-$MPFR_VERSION"
    extract_if_missing "mpc-$MPC_VERSION.tar.gz" "mpc-$MPC_VERSION"
    prepare_gcc_prereq "gmp-$GMP_VERSION" "gmp"
    prepare_gcc_prereq "mpfr-$MPFR_VERSION" "mpfr"
    prepare_gcc_prereq "mpc-$MPC_VERSION" "mpc"
    if [[ "$(uname -s)" != "Darwin" ]]; then
        download_if_missing "https://libisl.sourceforge.io/isl-$ISL_VERSION.tar.xz" "isl-$ISL_VERSION.tar.xz"
        extract_if_missing "isl-$ISL_VERSION.tar.xz" "isl-$ISL_VERSION"
        prepare_gcc_prereq "isl-$ISL_VERSION" "isl"
    fi
}

install_cross_toolchain() {
    run mkdir -p "$SRC_DIR"
    if [[ "$DRY_RUN" -eq 1 ]]; then
        log "Usaria $SRC_DIR como diretorio de fontes/build"
    fi
    if [[ "$DRY_RUN" -eq 0 ]]; then
        pushd "$SRC_DIR" >/dev/null
    fi
    download_if_missing \
        "https://ftp.gnu.org/gnu/binutils/binutils-$BINUTILS_VERSION.tar.xz" \
        "binutils-$BINUTILS_VERSION.tar.xz"
    download_if_missing \
        "https://ftp.gnu.org/gnu/gcc/gcc-$GCC_VERSION/gcc-$GCC_VERSION.tar.xz" \
        "gcc-$GCC_VERSION.tar.xz"
    extract_if_missing "binutils-$BINUTILS_VERSION.tar.xz" "binutils-$BINUTILS_VERSION"
    extract_if_missing "gcc-$GCC_VERSION.tar.xz" "gcc-$GCC_VERSION"
    if [[ -n "$LOCAL_DEPS_DIR" ]]; then
        prepare_local_gcc_prereqs
    fi
    build_binutils
    export PATH="$PREFIX/bin:$PATH"
    build_gcc
    if [[ "$DRY_RUN" -eq 0 ]]; then
        popd >/dev/null
    fi
}

verify_tool() {
    local tool="$1"
    if ! command -v "$tool" >/dev/null 2>&1; then
        die "Ferramenta ausente: $tool"
    fi
}

verify_installed_tools() {
    verify_tool clang
    verify_tool "$MAKE_BIN"
    if [[ "$INSTALL_HOST_DEPS" -eq 1 ]]; then
        verify_tool nasm
        verify_tool xorriso
    fi
    if [[ "$INSTALL_SMOKE_DEPS" -eq 1 ]]; then
        verify_tool qemu-system-x86_64
    fi
    if [[ "$INSTALL_CROSS_TOOLCHAIN" -eq 1 ]]; then
        export PATH="$PREFIX/bin:$PATH"
        verify_tool "$TARGET-gcc"
        verify_tool "$TARGET-ld"
        verify_tool "$TARGET-objcopy"
    fi
}

print_summary() {
    local gnu_efi_prefix="$BREW_PREFIX/opt/gnu-efi"
    cat <<EOF

[ok] Ambiente macOS preparado.

Comandos sugeridos:
  source ~/.zprofile
  make test HOST_CC=clang

Observacoes:
  - A plataforma oficial de validacao continua sendo VMware + UEFI + E1000.
  - O modo padrao evita Homebrew, QEMU, GNU GCC/binutils e bibliotecas bloqueadas.
  - Use --with-homebrew-deps para ISO local, --with-cross para TOOLCHAIN64=elf
    e --with-smoke para QEMU, apenas quando essas dependencias forem liberadas.
  - Homebrew nao fornece gnu-efi neste host; ISO/UEFI pode requerer EFI_PREFIX
    apontando para uma instalacao externa de gnu-efi.

EOF
}

main() {
    ensure_macos
    ensure_not_root
    log "Projeto detectado em $PROJECT_ROOT"
    if [[ -n "$LOCAL_DEPS_DIR" ]]; then
        LOCAL_DEPS_DIR="$(cd "$LOCAL_DEPS_DIR" && pwd)"
        log "Usando dependencias locais em $LOCAL_DEPS_DIR"
    fi
    ensure_xcode_tools
    if needs_homebrew; then
        ensure_homebrew
        load_homebrew_env
        install_packages
    else
        log "Modo macOS seguro: pulando Homebrew e dependencias opcionais"
    fi
    select_make
    if [[ "$INSTALL_CROSS_TOOLCHAIN" -eq 1 ]]; then
        if [[ "$INSTALL_PATH_EXPORT" -eq 1 ]]; then
            ensure_path_export
        fi
        install_cross_toolchain
    else
        log "Pulando instalacao da toolchain cruzada por solicitacao"
    fi
    if [[ "$DRY_RUN" -eq 0 ]]; then
        verify_installed_tools
    fi
    print_summary
}

main "$@"
