#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "$script_dir/../.." && pwd)"
workspace="${CAPYOS_SIBLING_WORKSPACE:-$(dirname "$repo_root")}"
build_dir="${CAPYOS_RELEASE_SIBLING_BUILD:-build}"
mode="${1:-all}"

# shellcheck source=release_siblings.env
source "$script_dir/release_siblings.env"

fail() {
  echo "[release-siblings] FAIL: $*" >&2
  exit 1
}

case "$mode" in
  plan|linked|all) ;;
  *) fail "usage: $0 [plan|linked|all]" ;;
esac

verify_tag() {
  local name="$1"
  local ref="$2"
  local destination="$workspace/$name"
  local actual_ref
  local actual_version
  local expected_version
  local untracked

  [[ -d "$destination/.git" ]] || fail "$name checkout is missing at $destination"
  actual_ref="$(git -C "$destination" describe --tags --exact-match HEAD 2>/dev/null || true)"
  [[ "$actual_ref" == "$ref" ]] || fail "$name is '$actual_ref', expected exact tag '$ref'"

  git -C "$destination" diff --quiet || fail "$name has modified tracked files"
  git -C "$destination" diff --cached --quiet || fail "$name has staged changes"
  untracked="$(git -C "$destination" ls-files --others --exclude-standard)"
  [[ -z "$untracked" ]] || fail "$name has untracked files"

  if [[ -f "$destination/VERSION" ]]; then
    expected_version="${ref#v}"
    actual_version="$(tr -d '\r\n' < "$destination/VERSION")"
    [[ "$actual_version" == "$expected_version" ]] || \
      fail "$name VERSION='$actual_version', expected '$expected_version' from $ref"
  fi
}

verify_tag "CapyUI" "$CAPYUI_REF"
verify_tag "CapyBrowser" "$CAPYBROWSER_REF"
verify_tag "CapyCodecs" "$CAPYCODECS_REF"
verify_tag "CapyAgent" "$CAPYAGENT_REF"
verify_tag "CapyLang" "$CAPYLANG_REF"
verify_tag "CapyBenchmark" "$CAPYBENCHMARK_REF"

[[ -f "$workspace/CapyUI/src/widget/capy_display_list.h" ]] || \
  fail "CapyUI widget contract is missing"
[[ -f "$workspace/CapyUI/src/desktop/desktop_runtime.c" ]] || \
  fail "CapyUI desktop session is missing"
[[ -f "$workspace/CapyBrowser/src/text/html_text.h" ]] || \
  fail "CapyBrowser text core is missing"
[[ -f "$workspace/CapyBrowser/src/displaylist/display_list.h" ]] || \
  fail "CapyBrowser graphical display-list contract is missing"
[[ -f "$workspace/CapyBrowser/src/page/page_render.c" ]] || \
  fail "CapyBrowser page pipeline is missing"
[[ -f "$workspace/CapyCodecs/src/image/capy_image.h" ]] || \
  fail "CapyCodecs image contract is missing"
[[ -f "$workspace/CapyCodecs/src/image/image.c" ]] || \
  fail "CapyCodecs image implementation is missing"

make_args=(
  "BUILD=$build_dir"
  "TOOLCHAIN64=${TOOLCHAIN64:-host}"
  "CAPYUI_DIR=$workspace/CapyUI"
  "CAPYBROWSER_DIR=$workspace/CapyBrowser"
  "CAPYCODECS_DIR=$workspace/CapyCodecs"
)

if [[ -n "$CAPYAI_REF" ]]; then
  verify_tag "CapyAI" "$CAPYAI_REF"
  [[ -f "$workspace/CapyAI/src/core/capy_ai_core.h" ]] || \
    fail "CapyAI core contract is missing"
  [[ -f "$workspace/CapyAI/capyai/data/model-capyos.capyaicore" ]] || \
    fail "CapyAI canonical model is missing"
  make_args+=("CAPYAI_DIR=$workspace/CapyAI")
else
  [[ ! -e "$workspace/CapyAI" ]] || \
    fail "CapyAI is present without a pinned CAPYAI_REF"
  make_args+=("CAPYAI_DIR=")
fi

cd "$repo_root"

if [[ "$mode" == "plan" || "$mode" == "all" ]]; then
  # Inspect the forced build plan. This catches a silent fallback before
  # spending time compiling an ISO.
  build_plan="$(make --no-print-directory -B -n "${make_args[@]}" capygfx-elf)"
  grep -Fq "$workspace/CapyBrowser/src/page/page_render.c" <<<"$build_plan" || \
    fail "Make did not detect/plan the CapyBrowser graphical page pipeline"
  grep -Fq "$workspace/CapyCodecs/src/image/image.c" <<<"$build_plan" || \
    fail "Make did not detect/plan the CapyCodecs image core"
  echo "[release-siblings] pinned refs detected in the capygfx build plan"
fi

if [[ "$mode" == "linked" || "$mode" == "all" ]]; then
  # The workflow's normal release/smoke target has now compiled the exact
  # ring-3 browser embedded by the full profile. Prove both sibling
  # implementations survived the link without doing a duplicate full build.

  if [[ "$build_dir" = /* ]]; then
    capygfx_elf="$build_dir/userland/bin/capygfx/capygfx.elf"
  else
    capygfx_elf="$repo_root/$build_dir/userland/bin/capygfx/capygfx.elf"
  fi
  [[ -s "$capygfx_elf" ]] || fail "compiled capygfx ELF is missing or empty"

  nm_tool="$(command -v x86_64-linux-gnu-nm || command -v nm || true)"
  [[ -n "$nm_tool" ]] || fail "nm is required to verify the linked browser"
  symbols="$($nm_tool "$capygfx_elf")"
  awk '{print $NF}' <<<"$symbols" | grep -Fxq "capy_page_render" || \
    fail "compiled capygfx does not contain CapyBrowser capy_page_render"
  awk '{print $NF}' <<<"$symbols" | grep -Fxq "capy_image_decode_memory" || \
    fail "compiled capygfx does not contain CapyCodecs capy_image_decode_memory"

  if [[ -n "$CAPYAI_REF" ]]; then
    if [[ "$build_dir" = /* ]]; then
      capyos_elf="$build_dir/capyos64.bin"
    else
      capyos_elf="$repo_root/$build_dir/capyos64.bin"
    fi
    [[ -s "$capyos_elf" ]] || fail "compiled CapyOS kernel is missing or empty"
    kernel_symbols="$($nm_tool "$capyos_elf")"
    awk '{print $NF}' <<<"$kernel_symbols" | grep -Fxq "capy_ai_predict" || \
      fail "compiled CapyOS does not contain the CapyAI native core"
    awk '{print $NF}' <<<"$kernel_symbols" | grep -Fxq "_binary_capyai_model_start" || \
      fail "compiled CapyOS does not contain the canonical CapyAI model"
  fi

  echo "[release-siblings] compiled browser/codecs and CapyAI core/model verified"
fi
