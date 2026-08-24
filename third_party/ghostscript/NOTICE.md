# Ghostscript runtime notice

Fovelle packages the AGPL Ghostscript 10.07.1 runtime for EPS decoding. The
runtime is obtained from the official Ghostscript release archive and is
staged into the application bundle by `dist/scripts/prepare-ghostscript.sh`.

The corresponding source archive is available from:

<https://github.com/ArtifexSoftware/ghostpdl-downloads/releases/download/gs10071/ghostscript-10.07.1.tar.gz>

Ghostscript is licensed under the GNU Affero General Public License, version 3
or later. The packaged bundle contains `licenses/Ghostscript-LICENSE` and the
build records the exact version and source URL in `runtime.json`.
