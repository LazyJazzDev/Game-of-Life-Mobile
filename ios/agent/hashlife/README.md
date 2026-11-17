Generic B3/S23 HashLife for Agent research scripts. No bundled patterns or metacell logic.

`life.js` is unmodified from https://github.com/copy/life at commit
`924c90afb529ad5d417f11d795bc1b400fff8d18`, copyright Fabian Hemmer,
under the BSD 2-clause license in `LICENSE`. The license is bundled with the app.
`adapter.js` provides bounded input/output and preserves cell coordinates.

The adapter also supplies generic B3/S23 RLE codecs. `parseRLE(text)` accepts
standard or headerless RLE and returns local cell coordinates; comment lines are
ignored. `encodeRLE(cells)` deduplicates cells and returns `{rle, origin}`, with
RLE normalized to the bounding box and `origin` retaining the signed offset.
Python scripts use `life_parse_rle` and `life_encode_rle`. Neither codec contains
patterns or source-specific adapters. Run `node ios/tests/research_life.cjs` to
check exact evolution and codec behavior.
