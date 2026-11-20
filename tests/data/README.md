# Regression fixtures

`remediation.gif` is an original four-frame 16×16 solid-color animation (red, green, blue, yellow), generated with Pillow and dedicated to the public domain under CC0-1.0. It exercises the Qt GIF reader separately from ImageIO APNG decoding.

`portable-hdr/` contains original encoded HDR/RAW signals, generator references, licensing and pinned hashes. See its README for the distinction between synthetic format coverage and vendor-camera coverage.

Existing `focus_transparency.png` remains unchanged.

`thumbnail-large-alpha.webp` is a synthetic 4200 × 2400 lossless WebP with an
opaque red upper half and a transparent lower half. It was generated with
`magick -size 4200x1200 xc:red -size 4200x1200 xc:none -append input.png`
and `cwebp -lossless input.png -o thumbnail-large-alpha.webp`.

`thumbnail-large-lossy-alpha.webp` uses the same source pixels with VP8X alpha.
Generate it with `cwebp -q 90 input.png -o thumbnail-large-lossy-alpha.webp`.

`thumbnail-42mp.webp` is a synthetic 7000 × 6000 lossless WebP. Its left half
is RGB (235, 35, 15) and its right half is RGB (15, 60, 215). It was encoded
with `cwebp -lossless` from a binary PPM.

`thumbnail-embedded-raw-prefix.bin` is an original DNG 1.4 test header and
128 × 64 RGB preview. Its raw SubIFD describes a 10000 × 6800, 16-bit
RGGB mosaic. The test appends the declared mosaic as a sparse zero-filled
tail. Orientation 6 rotates the preview's red-over-blue stripes into
blue-on-the-left and red-on-the-right.
