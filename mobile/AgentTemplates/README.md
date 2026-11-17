# OTCA metapixel templates

These two **pattern data** files implement B3/S23 using Brice Due's OTCA metapixel. They are not scaled drawings. `otca_b3s23_on` is the live macro state; `otca_b3s23_off` is the dead macro state. Both contain circuitry and are needed in a finite array.

- Cell data coordinates are normalized with the SAME offset (+5,+5).
- Each template is 2058 × 2058; adjacent origins are 2048 cells apart. The 10-cell overlaps are intentional; combine with live-cell union.
- For N × M macro cells, compose a rectangle `(2048*(N-1)+2058) × (2048*(M-1)+2058)`.
- On the current 16384 × 16384 canvas, at most 7 × 7 complete templates fit. A 5 × 5 blinker demo is 10250 × 10250.
- OFF is not empty: use OFF tiles for all dead positions. Do not place an OFF tile on top of an ON tile, as their state circuitry differs.
- Warm-up takes 35328 base generations with the initial macro state unchanged. Each following 35328 base generations advances one macro generation.
- Missing tiles are not an infinite macro plane. Leave a dead macro border and limit claims to the tested interior; a 5 × 5 blinker keeps a sufficient border for its oscillation.
- The mobile GPU currently steps at most 64 generations per tool call. Do not claim an entire macro cycle was simulated by a few such calls, or promise real-time macro evolution. HashLife was used ONLY as an independent desktop validation tool; it is not the app simulation backend.

## Source and license

Constructed from the unmodified Golly `Scripts/Python/metafier.py`, authored by Dave Greene with modifications by Andrew Trevorrow, implementing Brice Due's 2006 metapixel. Source revision: `53bdb74e48cd0792f87a2e3e6a5bb0c5d7d5af0e`.

Source: https://github.com/AlephAlpha/golly/blob/53bdb74e48cd0792f87a2e3e6a5bb0c5d7d5af0e/Scripts/Python/metafier.py
Supporting pattern operations: https://github.com/AlephAlpha/golly/blob/53bdb74e48cd0792f87a2e3e6a5bb0c5d7d5af0e/Scripts/Python/glife/__init__.py
Construction description: http://otcametapixel.blogspot.com/2006/05/how-does-it-work.html

The pattern data in this directory is supplied under GPL-2.0-or-later with the Golly copyright and license reproduced in COPYING.html. This is a separately attributed data collection; no Golly engine or source code is linked into the app. The app's original code retains its own license.

## Verification, 2026-11-17

The source's RuleBits for B3/S23 were added to BOTH states. Desktop Golly HashLife independently evolved a 5 × 5 array with three horizontal ON cells at macro row 2, columns 1–3. Counting live cells in each tile's central 512 × 512 region distinguished macro states at generations 0, 35328, 70656 and 105984. Observations were horizontal, horizontal (warm-up), vertical, horizontal, matching a B3/S23 blinker. This is a finite regression, not a proof for every possible macro configuration.

The construction adapter's coordinate preservation was separately checked with a translated glider after four generations. Extended RLE coordinates were retained for every intermediate evolution; ordinary RLE normalization would misalign intermediate components. Local construction and verification evidence remains in out/agent/metapixels/ and out/agent/metafier-adapter/.
