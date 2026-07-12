#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "$script_dir/../.." && pwd)"
workspace="${CAPYOS_SIBLING_WORKSPACE:-$(dirname "$repo_root")}"

# shellcheck source=release_siblings.env
source "$script_dir/release_siblings.env"

fail() {
  echo "[release-siblings] FAIL: $*" >&2
  exit 1
}

clone_tag() {
  local name="$1"
  local repository="$2"
  local ref="$3"
  local destination="$workspace/$name"
  local actual_ref
  local expected_version
  local actual_version

  [[ "$ref" == v* ]] || fail "$name ref must be an immutable v* tag, got '$ref'"
  [[ ! -e "$destination" ]] || fail "$destination already exists; refusing an ambiguous sibling checkout"

  if [[ "$name" == "CapyAI" && -n "${CAPYAI_DEPLOY_KEY:-}" ]]; then
    local key_file="${RUNNER_TEMP:-/tmp}/capyai-deploy-key"
    local known_hosts="${RUNNER_TEMP:-/tmp}/capyai-known-hosts"
    umask 077
    printf '%s\n' "$CAPYAI_DEPLOY_KEY" > "$key_file"
    # GitHub host key pinned from the authenticated /meta API. Do not use
    # ssh-keyscan here: the private sibling checkout must not trust first use.
    printf '%s\n' \
      'github.com ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAIOMqqnkVzrm0SdG6UOoqKLsabgH5C9okWi0dh2l9GKJl' \
      > "$known_hosts"
    if ! GIT_SSH_COMMAND="ssh -i $key_file -o IdentitiesOnly=yes -o StrictHostKeyChecking=yes -o UserKnownHostsFile=$known_hosts" \
      git clone --quiet --depth 1 --branch "$ref" \
        "git@github.com:$repository.git" "$destination"; then
      rm -f "$key_file"
      fail "$name private checkout failed"
    fi
    rm -f "$key_file"
  else
    git clone --quiet --depth 1 --branch "$ref" \
      "https://github.com/$repository.git" "$destination"
  fi

  actual_ref="$(git -C "$destination" describe --tags --exact-match HEAD 2>/dev/null || true)"
  [[ "$actual_ref" == "$ref" ]] || fail "$name checkout resolved to '$actual_ref', expected '$ref'"

  if [[ -f "$destination/VERSION" ]]; then
    expected_version="${ref#v}"
    actual_version="$(tr -d '\r\n' < "$destination/VERSION")"
    [[ "$actual_version" == "$expected_version" ]] || \
      fail "$name VERSION='$actual_version', expected '$expected_version' from $ref"
  fi

  echo "[release-siblings] $name pinned at $ref"
}

mkdir -p "$workspace"
clone_tag "CapyUI" "henriquefarisco/CapyUI" "$CAPYUI_REF"
clone_tag "CapyBrowser" "henriquefarisco/CapyBrowser" "$CAPYBROWSER_REF"
clone_tag "CapyCodecs" "henriquefarisco/CapyCodecs" "$CAPYCODECS_REF"
clone_tag "CapyAgent" "henriquefarisco/CapyAgent" "$CAPYAGENT_REF"
clone_tag "CapyLang" "henriquefarisco/CapyLang" "$CAPYLANG_REF"
clone_tag "CapyBenchmark" "henriquefarisco/CapyBenchmark" "$CAPYBENCHMARK_REF"

if [[ -n "$CAPYAI_REF" ]]; then
  clone_tag "CapyAI" "henriquefarisco/CapyAI" "$CAPYAI_REF"
  [[ -f "$workspace/CapyAI/src/core/capy_ai_core.h" ]] || \
    fail "CapyAI $CAPYAI_REF does not publish src/core/capy_ai_core.h"
else
  [[ ! -e "$workspace/CapyAI" ]] || \
    fail "CapyAI is present but CAPYAI_REF is empty; refusing an unpinned optional core"
  echo "[release-siblings] CapyAI intentionally disabled (no released core tag)"
fi
