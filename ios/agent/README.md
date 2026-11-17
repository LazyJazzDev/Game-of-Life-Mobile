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

Both platforms default to `deepseek-flash`, as requested by the user.

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

Protocol regressions: `node harmonyos/tests/agent.cjs` and
`node harmonyos/tests/research_find.cjs` cover premature final text, malformed
completion, waiting, cancellation, retained-document search beyond excerpts,
and the existing long-running loop behavior.
