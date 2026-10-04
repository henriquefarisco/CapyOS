#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "$script_dir/../.." && pwd)"
workspace="${CAPYOS_SIBLING_WORKSPACE:-$(dirname "$repo_root")}"
release_tag="${1:-}"

fail() {
  echo "[release-modules] FAIL: $*" >&2
  exit 1
}

[[ "$release_tag" == v* ]] || fail "usage: $0 v<capyos-release-tag>"

for repo in CapyAgent CapyAI CapyBrowser CapyCodecs CapyUI CapyLang CapyBenchmark; do
  [[ -f "$workspace/$repo/Makefile" ]] || fail "$repo checkout is missing"
done

mkdir -p "$repo_root/build"
staging_parent="$(mktemp -d "$repo_root/build/release-module-inputs.XXXXXX")"
python3 "$script_dir/stage_published_modules.py" \
  --output "$staging_parent/workspace" --release-tag "$release_tag"
workspace="$staging_parent/workspace"
capyai_version="$(sed -n 's/^version=//p' "$workspace/CapyAI/build/capypkg/org.capyos.ai.assistant.manifest")"
[[ -n "$capyai_version" ]] || fail "CapyAI version is empty"

mkdir -p "$repo_root/build/capypkg"

python3 "$script_dir/build_modules_index.py" \
  --workspace "$workspace" \
  --release-tag "$release_tag" \
  --output "$repo_root/build/capypkg/modules-index.txt"

cp "$workspace/CapyAI/build/capypkg/org.capyos.ai.assistant-$capyai_version.bin" \
  "$repo_root/build/capypkg/"

index="$repo_root/build/capypkg/modules-index.txt"
ai_bin="$repo_root/build/capypkg/org.capyos.ai.assistant-$capyai_version.bin"
ai_manifest="$workspace/CapyAI/build/capypkg/org.capyos.ai.assistant.manifest"

manifest_sha="$(sed -n 's/^payload_sha256=//p' "$ai_manifest" | tr -d '\r')"
manifest_size="$(sed -n 's/^payload_size=//p' "$ai_manifest" | tr -d '\r')"
actual_sha="$(sha256sum "$ai_bin" | awk '{print $1}')"
actual_size="$(wc -c < "$ai_bin" | tr -d '[:space:]')"
[[ "$manifest_sha" = "$actual_sha" ]] || fail "CapyAI manifest SHA-256 mismatch"
[[ "$manifest_size" = "$actual_size" ]] || fail "CapyAI manifest size mismatch"

python3 "$script_dir/verify_modules_index_assets.py" \
  --index "$index" \
  --release-tag "$release_tag" \
  --local-payload-dir "$repo_root/build/capypkg" \
  --allow-unsigned-index \
  --attempts 5 \
  --backoff-seconds 1 \
  --timeout 45

(
  cd "$repo_root/build/capypkg"
  sha256sum modules-index.txt "org.capyos.ai.assistant-$capyai_version.bin" \
    > modules.sha256
  sha256sum -c modules.sha256
)

echo "[release-modules] index and public CapyAI payload ready for $release_tag"
