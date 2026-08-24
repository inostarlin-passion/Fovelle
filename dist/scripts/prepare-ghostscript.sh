#!/usr/bin/env bash

# Stage the AGPL Ghostscript runtime used by the EPS loader inside an
# application bundle. A developer machine may provide gs, but a release build
# can also build the pinned source archive without requiring users to install
# anything at runtime.
set -euo pipefail

VERSION="10.07.1"
ARCHIVE_SHA256="2fc74362f9be6fae1b0a65d38fdcfd4f0b518cc3b07c5581fb661eb4d2e15251"
SOURCE_URL="https://github.com/ArtifexSoftware/ghostpdl-downloads/releases/download/gs10071/ghostscript-10.07.1.tar.gz"
OUTPUT=""

while [[ $# -gt 0 ]]; do
    case "$1" in
        --output)
            OUTPUT="${2:?--output requires a directory}"
            shift 2
            ;;
        *)
            echo "unknown argument: $1" >&2
            exit 2
            ;;
    esac
done

[[ -n "$OUTPUT" ]] || { echo "--output is required" >&2; exit 2; }

TEMP_ROOT="$(mktemp -d "${TMPDIR:-/tmp}/fovelle-ghostscript.XXXXXX")"
cleanup() {
    rm -rf "$TEMP_ROOT"
}
trap cleanup EXIT

runtime_root=""
ghostscript=""

if [[ -n "${FOVELLE_GHOSTSCRIPT:-}" ]]; then
    if [[ -x "$FOVELLE_GHOSTSCRIPT" ]]; then
        ghostscript="$FOVELLE_GHOSTSCRIPT"
    else
        echo "FOVELLE_GHOSTSCRIPT is not executable: $FOVELLE_GHOSTSCRIPT" >&2
        exit 1
    fi
elif command -v gs >/dev/null 2>&1; then
    ghostscript="$(command -v gs)"
fi

if [[ -n "$ghostscript" ]]; then
    ghostscript="$(realpath "$ghostscript")"
    runtime_root="$(cd "$(dirname "$ghostscript")/.." && pwd)"
fi

if [[ -z "$ghostscript" || ! -d "$runtime_root/share/ghostscript" ]]; then
    archive="$TEMP_ROOT/ghostscript-${VERSION}.tar.gz"
    curl --fail --location --silent --show-error "$SOURCE_URL" --output "$archive"
    # Verify the official release checksum before any source is built.
    printf '%s  %s\n' "$ARCHIVE_SHA256" "$archive" | shasum -a 256 --check --status
    tar -xzf "$archive" -C "$TEMP_ROOT"
    source_root="$TEMP_ROOT/ghostscript-${VERSION}"
    install_root="$TEMP_ROOT/install"
    (
        cd "$source_root"
        ./configure --prefix="$install_root"
        make -j"$(sysctl -n hw.ncpu)"
        make install
    )
    ghostscript="$install_root/bin/gs"
    runtime_root="$install_root"
fi

share_root="$runtime_root/share/ghostscript"
if [[ ! -d "$share_root" ]]; then
    echo "Ghostscript support files were not found below $runtime_root" >&2
    exit 1
fi

STAGE="$TEMP_ROOT/stage"
mkdir -p "$STAGE/bin" "$STAGE/lib" "$STAGE/share/ghostscript" "$STAGE/licenses"
cp "$ghostscript" "$STAGE/bin/gs"
chmod 755 "$STAGE/bin/gs"
cp -R "$share_root/." "$STAGE/share/ghostscript/"

if [[ -f "$runtime_root/LICENSE" ]]; then
    cp "$runtime_root/LICENSE" "$STAGE/licenses/Ghostscript-LICENSE"
elif [[ -f "${source_root:-}/doc/COPYING" ]]; then
    cp "${source_root}/doc/COPYING" "$STAGE/licenses/Ghostscript-LICENSE"
fi
cp "$(cd "$(dirname "$0")/../.." && pwd)/third_party/ghostscript/NOTICE.md" "$STAGE/licenses/NOTICE.md"

# Homebrew and many source builds link optional libraries dynamically. Copy
# non-system Mach-O dependencies into the bundle and rewrite their install
# names so the staged executable is independent of /opt/homebrew and PATH.
resolve_dependency() {
    local object="$1"
    local dependency="$2"
    local candidate

    if [[ "$dependency" == /* && -f "$dependency" ]]; then
        printf '%s\n' "$dependency"
        return 0
    fi

    if [[ "$dependency" == @loader_path/* ]]; then
        candidate="$(dirname "$object")/${dependency#@loader_path/}"
        [[ -f "$candidate" ]] && printf '%s\n' "$candidate"
        return 0
    fi

    if [[ "$dependency" == @executable_path/* ]]; then
        candidate="$STAGE/bin/${dependency#@executable_path/}"
        [[ -f "$candidate" ]] && printf '%s\n' "$candidate"
        return 0
    fi

    if [[ "$dependency" == @rpath/* ]]; then
        while IFS= read -r candidate; do
            [[ -n "$candidate" ]] || continue
            candidate="${candidate#\t}"
            candidate="${candidate#  }"
            [[ "$candidate" == /* ]] || continue
            candidate="$candidate/${dependency#@rpath/}"
            if [[ -f "$candidate" ]]; then
                printf '%s\n' "$candidate"
                return 0
            fi
        done < <(otool -l "$object" | awk '
            $1 == "cmd" && $2 == "LC_RPATH" { in_rpath = 1; next }
            in_rpath && $1 == "path" { print $2; in_rpath = 0 }
            in_rpath && $1 != "path" && $1 != "cmd" { next }
        ')

        # Some package managers omit an LC_RPATH even though the dependency
        # lives beside the package's other dylibs.
        candidate="/opt/homebrew/lib/${dependency#@rpath/}"
        [[ -f "$candidate" ]] && printf '%s\n' "$candidate"
    fi
}

copy_dependencies() {
    local object="$1"
    local dependency
    while IFS= read -r dependency; do
        [[ -n "$dependency" ]] || continue
        [[ "$dependency" == /usr/lib/* || "$dependency" == /System/* ]] && continue

        local resolved
        resolved="$(resolve_dependency "$object" "$dependency" || true)"
        [[ -f "$resolved" ]] || continue

        local name
        name="$(basename "$resolved")"
        local destination="$STAGE/lib/$name"
        if [[ ! -f "$destination" ]]; then
            cp "$resolved" "$destination"
            chmod 755 "$destination"
            install_name_tool -id "@loader_path/$name" "$destination"
            copy_dependencies "$destination"
        fi

        local replacement="@loader_path/$name"
        if [[ "$object" == "$STAGE/bin/gs" ]]; then
            replacement="@loader_path/../lib/$name"
        fi
        install_name_tool -change "$dependency" "$replacement" "$object" || true
    done < <(otool -L "$object" | tail -n +2 | awk '{print $1}')
}

if file -b "$STAGE/bin/gs" | grep -q 'Mach-O'; then
    copy_dependencies "$STAGE/bin/gs"
fi

rm -rf "$OUTPUT"
mkdir -p "$(dirname "$OUTPUT")"
mv "$STAGE" "$OUTPUT"

cat > "$OUTPUT/runtime.json" <<EOF
{
  "name": "Ghostscript",
  "version": "${VERSION}",
  "license": "GNU Affero General Public License version 3",
  "source_url": "${SOURCE_URL}",
  "executable": "bin/gs"
}
EOF
