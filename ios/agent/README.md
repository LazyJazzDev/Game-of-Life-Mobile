# Life Agent research runtime

The Agent starts with no pattern templates or construction recipes. Its tools
provide canvas editing/inspection, user-authored RLE templates and composition,
public HTTPS document retrieval, public repository discovery, and an in-memory
research workspace. `research_search` queries GitHub repository metadata and
returns compact source URLs; it does not supply a preselected circuit, template,
or construction recipe. Search results remain untrusted source material.

Research scripts run in disposable Web Workers with a 60-second deadline.
JavaScript receives plain-text `documents` and generic `life.evolve` (B3/S23
HashLife). Python uses the unmodified Pyodide 0.27.7 WebAssembly runtime from
`https://cdn.jsdelivr.net/pyodide/v0.27.7/full/`, with `documents` and
`life_evolve`. Python has a virtual filesystem, not access to the application's
files or credential. No Golly adapter, metacell algorithm, or pattern data is
installed. Any adapters and templates must be authored or retrieved by the
Agent during the run.

Pyodide source and MPL-2.0 license:
https://github.com/pyodide/pyodide/tree/0.27.7
https://github.com/pyodide/pyodide/blob/0.27.7/LICENSE

Bundled HashLife source and BSD notice are in `hashlife/`.

The build copies the local `LIFE_AGENT_KEY_FILE` into `AgentCredential.key`.
The credential is not tracked in Git and has no editing control in the app.
Network consent remains a separate user setting.

For reproducible simulator trials, uninstall and reinstall before **each** run.
Never seed a new trial with another run's documents, scripts, templates, canvas,
or conversation. Simulator-only `LONGMARCH_AGENT_SMOKE` accepts the task prompt
and writes `AgentProgress.json` plus a final `AgentSmoke.json`; these reports
contain tool inputs/results, not the API credential. `LONGMARCH_RESEARCH_SELFTEST`
executes a supplied Python test in the same research runtime, with its result
written to `ResearchSelftest.json`. Reset again after a self-test.

The disposable-worker implementation is shared with HarmonyOS in
`mobile/agent-research.js`. iOS bundles this file directly; HarmonyOS stages it
with `harmonyos/prepare_agent.py`. Platform code handles document fetching,
worker hosting and native template saves.

Both platforms use OpenAI `gpt-6.1-sol` with high reasoning through the Responses
API. The local credential is `tools/openai-api.key` (iOS can override this with
`LIFE_AGENT_KEY_FILE`; reconfigure existing build directories when switching keys).
Requests use `store: false` and sequential function calls. Complete response
items, including assistant phase and any encrypted reasoning, are replayed with
`function_call_output`; output-only provenance is removed. Only completed
responses can execute tools; incomplete output never executes partial calls.

Network failures and Stop preserve the original request and completed tool
chains in memory. An interrupted batch is removed from protocol history and the
next run is told to inspect actual state before repeating edits. This preserves
context for “continue” without blindly replaying actions. Research documents
remain in memory for the lifetime of this Agent instance; this is not disk-based
recovery after process termination.

Research responses contain bounded excerpts. `research_find` searches the full
retained documents (including repository indexes) and returns document IDs,
character offsets, and surrounding text for follow-up reads. It treats the query
literally and limits each result to 20 matches.

A prose-only model response no longer ends the tool loop. The Agent audits the
original request and continues unfinished work. `task_complete` requires a final
summary and nonempty evidence; `task_wait` distinguishes genuinely missing user
information or destructive confirmation from completion. Stop still cancels the
loop. These are model-reported outcomes, not an independent correctness proof:
construction trials must still be checked against actual evolution and inputs.
The same tool and completion contracts are implemented on both platforms.

## Eight-bit adder validation (iOS build 36)

A factory-reset simulator trial with OpenAI independently discovered
[`bespoyasov/binary-full-adder-in-the-game-of-life`](https://github.com/bespoyasov/binary-full-adder-in-the-game-of-life)
(reference revision `e64d940d154a24808636f83b224f5d6c4e04fd54`). The Agent
adapted its cell patterns into eight physically connected full-adder stages,
corrected the carry routing and phase alignment, and saved the resulting RLEs.
The prompt supplied the task and sample inputs, not the source or circuit recipe.

The final `v2` circuit uses invariant wiring and 16 independently optional input
guns. A present gun encodes 1; an absent gun encodes 0. In the saved fixed-core
coordinate frame, bit `i` runs from 0 (least significant, top) to 7 (bottom):

| Item | Position / extent |
| --- | --- |
| A input gun | `(324 + 182*i, 120 + 1247*i)` |
| B input gun | `(325 + 182*i, 242 + 1247*i)` |
| Output window | `(710 + 182*i, 945 + 1247*i)`, size `43 × 48` |

Start from `Adder8_FixedCore_v2`; overlay `Adder8_InputEven_v2` for even bit
indices and `Adder8_InputOdd_v2` for odd indices at each enabled input position.
Both input templates have origin `(0, 0)` and size `27 × 39`. Evolve B3/S23 for
65520 generations. An output glider stream encodes 1; no output stream encodes 0.
Overflow is discarded. For 187 + 53, outputs from bit 0 to bit 7 are `00001111`,
or `11110000` in conventional order: 240. Host arithmetic is used only to check
expected test answers, not to construct output or intermediate carry signals.

Independent native Golly HashLife validation passed 111 distinct input pairs,
including carry chains and overflow, plus 15 checks at later generation counts.
Every case also verified that changing inputs changes only the optional guns.
The sample's initial and generation-65520 cell sets matched the saved templates
exactly. Output decoding checked actual glider shapes rather than arbitrary
nonempty regions. This is sampled validation, not exhaustive testing of all
65536 input pairs.

The main native canvas additionally advanced the side-by-side initial/result
demonstration by 60 generations and matched its predicted population and hash;
all eight native output windows returned 240. This does not imply the complete
circuit has a 60-generation period. Local trial evidence, RLE templates, and the
independent report are in `out/agent/adder-ios36/`; the verifier is
`out/agent/verify_adder36.py`. These are local verification artifacts, not bundled
Agent knowledge. The simulator remains available for inspection; Mate 70 was
not used for this trial.

Protocol regressions: `node harmonyos/tests/agent.cjs` and
`node harmonyos/tests/research_find.cjs` cover premature final text, malformed
completion, waiting, cancellation, retained-document search beyond excerpts,
and the existing long-running loop behavior.
