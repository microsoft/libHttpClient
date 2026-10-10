#!/usr/bin/env bash
# Restores the third-party sources libHttpClient builds from (asio, boost-wintls, curl, openssl,
# websocketpp, zlib) into External/<name>, at the pins in ports/libhttpclient-deps/deps.cmake.
# Bash twin of Restore-Vcpkg.ps1.
#
# Builds run this automatically (CMake: Build/vcpkg/AutoRestore.cmake; Linux: libHttpClient_Linux.bash;
# Xcode: a build phase), so you normally never call it yourself. It is a fast no-op when External/
# is already up to date.
#
# By default sources are downloaded from GitHub; no Microsoft-internal access is needed.
#   --block-origin (or HC_VCPKG_BLOCK_ORIGIN=1): fetch only from Microsoft's Terrapin mirror. Internal CI.
#   --force: restore even if External/ looks up to date.
# Set HC_SKIP_VCPKG_RESTORE=1 to disable the automatic restore in builds.
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "$script_dir/../.." && pwd)"
deps_file="$script_dir/ports/libhttpclient-deps/deps.cmake"
stamp_file="$repo_root/External/.vcpkg-deps.stamp"
terrapin="https://vcpkg.storage.devpackages.microsoft.io/artifacts/"

block_origin="${HC_VCPKG_BLOCK_ORIGIN:-0}"
force=0
while [ $# -gt 0 ]; do
    case "$1" in
        --block-origin) block_origin=1; shift;;
        --force) force=1; shift;;
        *) echo "unknown arg: $1" >&2; exit 1;;
    esac
done

dep_paths=()
while IFS= read -r p; do dep_paths+=("$p"); done < <(sed -nE 's/^[[:space:]]*set\(HC_DEP_[^[:space:]]+_PATH[[:space:]]+([^[:space:])]+)\).*/\1/p' "$deps_file")
if [ "${#dep_paths[@]}" -eq 0 ]; then
    echo "No HC_DEP_*_PATH entries found in '$deps_file'." >&2
    exit 1
fi

up_to_date() {
    [ -f "$stamp_file" ] || return 1
    cmp -s "$stamp_file" "$deps_file" || return 1
    local p
    for p in "${dep_paths[@]}"; do
        [ -d "$repo_root/$p" ] || return 1
    done
}

# Parallel builds can call this concurrently; mkdir is atomic everywhere (flock isn't on macOS).
mkdir -p "$repo_root/External"
lock_dir="$repo_root/External/.vcpkg-restore.lock"
acquired_lock=0
for _ in $(seq 1 1800); do
    if mkdir "$lock_dir" 2>/dev/null; then acquired_lock=1; break; fi
    sleep 1
done
if [ "$acquired_lock" -eq 0 ]; then
    echo "Timed out waiting for '$lock_dir'; remove it if no restore is running." >&2
    exit 1
fi
trap 'rmdir "$lock_dir" 2>/dev/null || true' EXIT

if [ "$force" -eq 0 ] && up_to_date; then
    echo "libHttpClient third-party sources are up to date."
    exit 0
fi

echo "Restoring libHttpClient third-party sources (${dep_paths[*]})..."

# shellcheck source=/dev/null
source "$script_dir/bootstrap-vcpkg.sh"

install_root="$script_dir/vcpkg_installed"
vcpkg_args=(install "--x-manifest-root=$script_dir" "--x-install-root=$install_root")
if [ "$block_origin" = "1" ]; then
    vcpkg_args+=("--x-asset-sources=clear;x-azurl,$terrapin;x-block-origin")
fi
"$XBBL_VCPKG_ROOT/vcpkg" "${vcpkg_args[@]}"

share=()
for d in "$install_root"/*/share/libhttpclient-deps/src; do
    [ -d "$d" ] && share+=("$d")
done
if [ "${#share[@]}" -ne 1 ]; then
    echo "Expected one restored libhttpclient-deps tree under '$install_root', found ${#share[@]}." >&2
    exit 1
fi

for p in "${dep_paths[@]}"; do
    if [ ! -d "${share[0]}/$p" ]; then
        echo "Restored tree is missing '$p'." >&2
        exit 1
    fi
    # A leftover git submodule checkout (from before the move to vcpkg) is replaced as well.
    rm -rf "${repo_root:?}/$p"
    mkdir -p "$(dirname "$repo_root/$p")"
    cp -R "${share[0]}/$p" "$repo_root/$p"
done

cp "$deps_file" "$stamp_file"
echo "libHttpClient third-party sources restored under '$repo_root/External'."
