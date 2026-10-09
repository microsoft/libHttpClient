#!/usr/bin/env bash
# Bootstraps the pinned vcpkg tool (bash twin of Bootstrap-Vcpkg.ps1).
# Honors XBBL_VCPKG_ROOT if set; otherwise clones into ~/.tools/vcpkg.
# Adapted from PlayFab.C Build/vcpkg/bootstrap-vcpkg.sh @ bbe5fbbb. Change: the default vcpkg root is ~/.tools/vcpkg (shared by every repo) instead of <repo>/.tools/vcpkg.
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "$script_dir/../.." && pwd)"
pin_file="$script_dir/vcpkg-pin.json"

get_pin() { grep -oE "\"$1\"[[:space:]]*:[[:space:]]*\"[^\"]+\"" "$pin_file" | sed -E 's/.*"([^"]+)"[[:space:]]*$/\1/'; }
repository="$(get_pin repository)"
commit="$(get_pin commit)"
tag="$(get_pin tag)"

# Git hooks and rebase --exec export these; they would redirect our commands at another repo.
# Clear them around each call only: this script is sourced, so the caller keeps its own values.
vcpkg_git() {
    env -u GIT_DIR -u GIT_WORK_TREE -u GIT_COMMON_DIR -u GIT_INDEX_FILE \
        -u GIT_OBJECT_DIRECTORY -u GIT_ALTERNATE_OBJECT_DIRECTORIES git "$@"
}

# The checks below mix text and inode comparisons, so resolve the paths first.
# CDPATH would make cd pick a different directory and echo it, so disable it per call.
resolve_dir() {
    local path="$1" parent base
    if [ -d "$path" ]; then
        (CDPATH= ; cd -- "$path" && pwd -P)
        return
    fi

    parent="$(dirname -- "$path")"
    base="$(basename -- "$path")"
    if [ -d "$parent" ]; then
        printf '%s/%s\n' "$(CDPATH= ; cd -- "$parent" && pwd -P)" "$base"
    else
        printf '%s\n' "$path"
    fi
}

vcpkg_root="$(resolve_dir "${XBBL_VCPKG_ROOT:-$HOME/.tools/vcpkg}")"
default_root="$HOME/.tools/vcpkg"

# Symlinks give the repo two spellings, so check the vcpkg root against both.
repo_root_real="$(resolve_dir "$repo_root")"

# The steps below rewrite the remote and working tree, so keep the vcpkg root out of this repo.
assert_outside_repo() {
    local root="$1" candidate
    for candidate in "$repo_root/" "$repo_root_real/"; do
        case "$candidate" in
            "${root%/}"/*)
                echo "XBBL_VCPKG_ROOT '$root' contains this repository; point it at a directory outside the repo." >&2
                exit 1
                ;;
        esac
    done
}

assert_outside_repo "$vcpkg_root"

# A broken .git makes git fall back to the enclosing repo, so require a checkout rooted here.
# A .git file instead of a directory belongs to a linked worktree or submodule owned elsewhere.
is_vcpkg_checkout() {
    local path="$1"
    local cdup
    [ -d "$path/.git" ] || return 1
    cdup="$(vcpkg_git -C "$path" rev-parse --show-cdup 2>/dev/null)" || return 1
    [ -z "$cdup" ]
}

# Rewriting the remote and checking out the pin would destroy an unrelated repository, so make
# sure the checkout really is vcpkg before adopting it.
is_vcpkg_repository() {
    local path="$1" origin
    # An interrupted bootstrap leaves a commit-less checkout; adopt it so the next run can resume.
    vcpkg_git -C "$path" rev-parse --verify HEAD >/dev/null 2>&1 || return 0
    origin="$(vcpkg_git -C "$path" remote get-url origin 2>/dev/null || true)"
    [ "$origin" = "$repository" ] && return 0
    # A clone from a mirror or a fork has a different remote, so fall back to the vcpkg layout.
    [ -d "$path/ports" ] && [ -d "$path/triplets" ]
}

unusable_reason=""
if [ -d "$vcpkg_root" ]; then
    if ! is_vcpkg_checkout "$vcpkg_root"; then
        unusable_reason="is not a git checkout rooted at that path"
    elif ! is_vcpkg_repository "$vcpkg_root"; then
        unusable_reason="is a git checkout but does not look like vcpkg"
    fi
fi

if [ -n "$unusable_reason" ]; then
    if [ "$vcpkg_root" != "$(resolve_dir "$default_root")" ]; then
        echo "XBBL_VCPKG_ROOT '$vcpkg_root' $unusable_reason; point it at a vcpkg checkout or an empty directory." >&2
        exit 1
    fi

    # Deleting through a link would take out whatever it points at.
    if [ -L "$default_root" ]; then
        echo "'$default_root' is a link to '$vcpkg_root'; remove it and run this script again." >&2
        exit 1
    fi

    echo "'$vcpkg_root' $unusable_reason; recreating it."
    rm -rf "$vcpkg_root"
fi

if [ ! -d "$vcpkg_root" ]; then
    mkdir -p "$vcpkg_root"
    # Creating missing parents can change where the path resolves to.
    vcpkg_root="$(resolve_dir "$vcpkg_root")"
    assert_outside_repo "$vcpkg_root"
    vcpkg_git -C "$vcpkg_root" init -q
fi

if ! is_vcpkg_checkout "$vcpkg_root"; then
    echo "'$vcpkg_root' is not a git checkout rooted at that path; refusing to run git commands that would affect the enclosing repository." >&2
    exit 1
fi

if vcpkg_git -C "$vcpkg_root" remote get-url origin >/dev/null 2>&1; then
    vcpkg_git -C "$vcpkg_root" remote set-url origin "$repository"
else
    vcpkg_git -C "$vcpkg_root" remote add origin "$repository"
fi

current="$(vcpkg_git -C "$vcpkg_root" rev-parse --verify HEAD 2>/dev/null || true)"
if [ "$current" != "$commit" ]; then
    vcpkg_git -C "$vcpkg_root" fetch --depth 1 origin "$commit"
    # An interrupted checkout leaves this lock behind, and git then refuses to touch the index.
    if [ -e "$vcpkg_root/.git/index.lock" ]; then
        echo "Removing leftover '$vcpkg_root/.git/index.lock'."
        rm -f "$vcpkg_root/.git/index.lock"
    fi
    vcpkg_git -C "$vcpkg_root" checkout --force --detach FETCH_HEAD
fi

current="$(vcpkg_git -C "$vcpkg_root" rev-parse HEAD)"
if [ "$current" != "$commit" ]; then
    echo "Expected vcpkg commit '$commit', but found '$current'." >&2
    exit 1
fi

# The checkout may have moved to a new pin while keeping a vcpkg binary built for the old one, so
# compare the binary against the tool release the checked-out scripts expect.
expected_tool_tag=""
tool_metadata="$vcpkg_root/scripts/vcpkg-tool-metadata.txt"
if [ -f "$tool_metadata" ]; then
    expected_tool_tag="$(sed -nE 's/^[[:space:]]*VCPKG_TOOL_RELEASE_TAG[[:space:]]*=[[:space:]]*([^[:space:]]+)[[:space:]]*$/\1/p' "$tool_metadata" | head -n 1)"
fi

vcpkg_tool_current() {
    local version_output
    [ -x "$vcpkg_root/vcpkg" ] || return 1
    [ -n "$expected_tool_tag" ] || return 0
    version_output="$("$vcpkg_root/vcpkg" version 2>/dev/null)" || return 1
    grep -qE "version[[:space:]]+${expected_tool_tag//./\\.}(-|[[:space:]]|$)" <<<"$version_output"
}

if ! vcpkg_tool_current; then
    if [ -e "$vcpkg_root/vcpkg" ]; then
        echo "vcpkg tool at '$vcpkg_root/vcpkg' does not match expected release '$expected_tool_tag'; re-bootstrapping."
    fi
    "$vcpkg_root/bootstrap-vcpkg.sh" -disableMetrics
    if ! vcpkg_tool_current; then
        echo "vcpkg bootstrap did not produce tool release '$expected_tool_tag' at '$vcpkg_root/vcpkg'." >&2
        exit 1
    fi
fi

export XBBL_VCPKG_ROOT="$vcpkg_root"
echo "vcpkg $tag is ready at '$vcpkg_root' ($current)."