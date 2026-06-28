# Build Brief: LocalVocal Broadcast-Captions Fork

You are extending the **obs-localvocal** OBS plugin (C++, builds as an OBS Studio
plugin) into a broadcast-grade local-captioning plugin. The transcription engine
(whisper.cpp + Silero VAD) already works and is **not** to be touched except at the
clearly defined seams below. Your job is to rework how captions are *buffered,
formatted, labelled, written to file, displayed, and sourced*, and to add a
multi-source mix mode and a remote-whisper-server option.

Work in **layers, in the order given**. Build and confirm each layer compiles and
behaves before starting the next. Do not scaffold all seven features at once.

---

## 0a. Setup Fork setup as BOSSCAT-localvocal


```bash
# Clone upstream, but name the working copy after the fork.
git clone https://github.com/locaal-ai/obs-localvocal.git BOSSCAT-localvocal
cd BOSSCAT-localvocal

# Preserve a reference to upstream for future merges, then detach our origin.
git remote rename origin upstream
git remote add origin https://github.com/unclescunter/BOSSCAT-localvocal.git

# Record the exact upstream commit we forked from (for the CHANGES file / attribution).
git rev-parse HEAD > .forked-from-commit.txt
git log -1 --format='%H %ci' upstream/HEAD 2>/dev/null || git rev-parse HEAD
```

> Do **not** delete upstream history. Keeping it intact is both a GPL courtesy and
> what lets us pull future improvements from Roy's project.

### 0b. Baseline build before any changes (you can skip this we built the plugin already)

Follow the upstream build instructions (CMake + OBS/libobs + ONNX Runtime +
whisper.cpp; see `.github/scripts` and `cmake/`). **Confirm the unmodified clone
builds and loads in OBS first.** Do not proceed to Layer 1 until the stock plugin
compiles and runs — this is the known-good baseline we differentiate against.
The known good instruction is 
```rm -rf build_x86_64
export CFLAGS="-fPIC"
export CXXFLAGS="-fPIC"
cmake -B build_x86_64 --preset linux-x86_64 -DCMAKE_INSTALL_PREFIX=./release -DCMAKE_POSITION_INDEPENDENT_CODE=ON
cmake --build build_x86_64 --target install
```
 try no other instructions this is the only one that works.

### 0c. Create the fork branch and the new GitHub repo

```bash
git checkout -b bosscat-main

# Create the repo on the user's GitHub (requires gh auth login as 'unclescunter').
gh repo create unclescunter/BOSSCAT-localvocal \
  --public \
  --description "BOSSCAT-localvocal — a broadcast-captions fork of obs-localvocal (GPL-2.0)" \
  --source . --remote origin
# If gh is unavailable, create the empty repo manually on github.com and:
#   git push -u origin bosscat-main
```

### 0d. License & attribution hygiene (do this in the first commit)

GPL-2.0 requires: keep the license, keep original copyright notices, and clearly
mark our changes. Professional courtesy goes beyond the minimum — we credit Roy
prominently and link the original.

1. **Keep `LICENSE` exactly as-is** (GNU GPL v2). Do not alter or relicense it.
2. **Do not remove** any existing copyright headers, `buildspec.json` author field
   (`Roy Shilkrot`), or `bundleId`. You will *add* a fork notice, not replace
   identity wholesale. (You may add a distinct bundleId/plugin display name for the
   fork — `BOSSCAT-localvocal` — so it installs side-by-side, while leaving the
   original author credit in place.)
3. **Add `CHANGES.md`** at repo root stating this is a fork, the upstream commit it
   derives from (`.forked-from-commit.txt`), and a running list of significant
   modifications (the seven feature layers). This is the GPL "state changes"
   requirement, done well.
4. **Add a fork-notice header comment** to every source file you substantially
   modify, e.g.:
   ```
   // This file is part of BOSSCAT-localvocal, a GPL-2.0 fork of obs-localvocal
   // by Roy Shilkrot (https://github.com/locaal-ai/obs-localvocal).
   // Modifications (c) 2025 unclescunter, licensed GPL-2.0. See CHANGES.md.
   ```
   Leave files you don't touch unmodified.
5. **Replace `README.md`** with the fork README (provided as `README_FORK.md` in the
   deliverables) — it leads with credit and a link to the original, explains the
   relationship, and only then describes the fork's added features.
6. For any code adapted from **obs-captions-plugin** (the regex replace filter and
   the word-wrap algorithm — also GPL-2.0, by "RatWithAShotgun"), add a comment at
   those call sites crediting that project too, and list it in `CHANGES.md`.

First commit message:
```
fork: initialize BOSSCAT-localvocal from obs-localvocal (GPL-2.0)

Fork of obs-localvocal by Roy Shilkrot (https://github.com/locaal-ai/obs-localvocal).
Adds CHANGES.md, fork README, and fork notices. License unchanged (GPL-2.0).
Upstream commit recorded in .forked-from-commit.txt.
```


Follow the repo's existing build instructions (CMake + OBS/libobs + ONNX Runtime +
whisper.cpp deps; see `.github/scripts` and `cmake/`). Confirm a clean build of
**unmodified** master first so you have a known-good baseline. Do not proceed until
the stock plugin compiles and loads in OBS.

Keep commits small — one per layer, ideally one per sub-step — so regressions are
bisectable.

---

## Key files and the seams you will use

These line numbers are from the cloned master; treat them as starting anchors, not
gospel — re-grep if they have drifted.

- **`src/transcription-filter.c`** — `obs_source_info transcription_filter_info`.
  This is the filter registration (id `transcription_filter_audio_filter`). Audio
  filters are owned by one source; remember this for Layer 3.
- **`src/transcription-filter.cpp`**
  - `transcription_filter_filter_audio()` (~line 116) — where PCM frames arrive and
    are pushed into `gf->input_buffers[c]`. This is the audio-capture seam for the
    multi-source mix (Layer 3).
  - `captions_monitor.initialize(...)` (~line 362) — where the caption buffer thread
    is constructed with `(num_lines, num_chars_per_line, std::chrono::seconds(3),
    segmentation)`. The hard-coded `3` is the decay you will make configurable.
- **`src/whisper-utils/whisper-processing.cpp`**
  - `run_whisper_inference(gf, pcm32f_data, size, start_offset_ms, ...)` (~line 140)
    returns `DetectionResultWithText`. **This is the inference seam for Layer 4**
    (remote server). Do not modify whisper internals; add a sibling path and branch
    to it.
  - `run_inference_and_callbacks()` (~line 339) and `whisper_loop()` (~line 374) —
    the loop that calls inference then routes results.
- **`src/whisper-utils/whisper-processing.h`** — `struct DetectionResultWithText`
  `{ DetectionResult result; std::string text; uint64_t start_timestamp_ms;
  uint64_t end_timestamp_ms; ... }`. Use these timestamps for SRT (Layer 5).
- **`src/whisper-utils/token-buffer-thread.{h,cpp}`** — the caption buffer. **You
  will substantially rewrite this in Layer 1.**
- **`src/transcription-filter-callbacks.cpp`**
  - `send_caption_to_source(target_source_name, caption, gf)` (~line 35) — pushes a
    finished caption string into an OBS text source. Your buffer's emit callback
    ends here.
  - `output_text()` path (~lines 300–320) — the branch that currently chooses
    buffered vs non-buffered output and calls `addSentenceFromStdString()` or
    `send_caption_to_source()` directly.
  - `send_sentence_to_file()` (~line 139) — existing txt/srt writer. **You will
    replace its trigger logic in Layer 5.**
  - `recording_state_callback()` (~line 658) — already hooks
    `OBS_FRONTEND_EVENT_RECORDING_STARTING / STOPPING / STOPPED` and calls
    `obs_frontend_get_last_recording()`. **This is your SRT-per-recording hook.**
- **`src/transcription-filter-properties.cpp`** — all filter UI. Model selection
  list + `whisper_model_path_external` path (~lines 206–219); buffered-output group
  (~lines 477–540: `buffer_num_lines`, `buffer_num_chars_per_line`, type). You add
  all new properties here.
- **`src/transcription-filter-data.h`** — `transcription_filter_data` struct; add
  new config fields here. Existing relevant fields: `save_to_file`, `save_srt`,
  `truncate_output_file`, `output_file_path`, `sentence_number`,
  `start_timestamp_ms`, `buffered_output`, `captions_monitor`.
- **`src/ui/filter-replace-dialog.{cpp,h,ui}`** + `filter-replace-utils.{cpp,h}`
  — existing regex find/replace UI and engine. **Reuse this for Layer 1's replace
  hook**; do not rebuild it.
- **`src/transcription-filter-utils.cpp`** — already uses
  `obs_frontend_get_current_scene()` and frontend event callbacks; reference for
  Layer 6's dock.
- **`data/locale/en-US.ini`** (and `en-GB.ini`) — add a translation key for every
  new property label. Match the existing `MT_(...)` key pattern.

---

## LAYER 1 — Caption engine: fuzzy wrap + per-word emit + decay + replace + label

**Rewrite `src/whisper-utils/token-buffer-thread.cpp`** (and adjust its header).
This is the foundation; everything renders through it.

The stock implementation streams *characters* into a queue and re-emits on every
tick → flicker, and its character cap does not reliably hold. Replace that model
with: hold the latest text as a string, format it, and emit the formatted caption
**only when the formatted string changes** (since whisper grows text word-by-word,
this yields ~one emit per word).

### 1a. Fuzzy word-wrap (validated algorithm — implement exactly this)

```cpp
// Greedy word wrap to a SOFT character target. Fill a line with whole words
// until adding the next word would exceed soft_target; then break. Never split a
// word unless a single word alone exceeds the line, in which case hard-split it.
static std::vector<std::string> wrap_to_lines(const std::string &text, size_t soft_target)
{
    std::vector<std::string> lines;
    if (soft_target == 0) { lines.push_back(text); return lines; }
    std::istringstream iss(text);
    std::string word, line;
    auto flush = [&]() { lines.push_back(line); line.clear(); };
    while (iss >> word) {
        const size_t projected = line.size() + (line.empty() ? 0 : 1) + word.size();
        if (projected <= soft_target) {
            if (!line.empty()) line += " ";
            line += word;
        } else if (word.size() > soft_target) {       // word longer than a line
            if (!line.empty()) { if (line.size() + 2 <= soft_target) line += " "; else flush(); }
            for (char c : word) { if (line.size() + 1 <= soft_target) line += c; else { flush(); line += c; } }
        } else { if (!line.empty()) flush(); line = word; }
    }
    if (!line.empty()) lines.push_back(line);
    return lines;
}
```

This is the **soft** limit (target 35). It will let a line land anywhere up to the
last whole word that fits — e.g. "This is a 32 character sentence," then break.
The **hard line cap** is applied after wrapping: keep only the last `numSentences`
lines (`lines.erase(lines.begin(), lines.end() - numSentences)` when oversized).

This algorithm has been verified to: cap every line at/under the soft target, keep
words intact, hard-split only over-length words, and never exceed the line count.
Mirror it faithfully.

### 1b. Per-word emit + decay (monitor loop)

- Store the working text as two strings under a mutex: `workingFinal` (committed
  finals, space-joined) and `workingPartial` (current refining tail). On a final
  result append to `workingFinal` and clear `workingPartial`; on a partial, replace
  `workingPartial`.
- `addSentenceFromStdString(sentence, start, end, is_partial)` collapses newlines to
  spaces, updates the working strings, stamps `lastContributionTime`, sets a
  `newDataAvailable` flag.
- The monitor thread loop (poll every **180 ms** — just under 0.2 s, comfortably
  above 2.5 words/s so flow is continuous):
  1. If `newDataAvailable`: build `renderCaption()` = run replace (1c) → wrap (1a) →
     keep last N lines → join with `\n`. Emit via the presentation callback **only
     if it differs from the last emitted string**. Stamp `lastCaptionTime`.
  2. Else if `maxTime > 0` and a caption is showing and `now - lastCaptionTime >
     maxTime`: emit "" (clear). (Compute "should clear" under the lock, release,
     then call the clear path — do **not** hold the lock across the callback.)
- Keep the public method signatures (`initialize`, `addSentence`,
  `addSentenceFromStdString`, `clear`, `setNumSentences`, `setNumPerSentence`,
  `setSegmentation`, `setCaptionPresentationCallback`, `stopThread`) so the rest of
  the plugin keeps compiling. Segmentation may collapse to word-mode only;
  keep the parameter for API compatibility.

### 1c. Replace hook

Before wrapping, run the working text through the existing replace engine
(`filter-replace-utils`). The filter list already lives in settings and has a UI
dialog — reuse both. Apply replacements to the full working string each render so
profanity/custom swaps appear live, per-word.

### 1d. Label prefix

Add a `std::string label` + `bool label_enabled` to the buffer (fed from settings,
Layer 2). When enabled and non-empty, prefix the **first line** with `label + ": "`
*before* wrapping so the label counts toward wrapping. (Group label for Layer 3.)

**Verify Layer 1** by extracting `wrap_to_lines` into a tiny standalone test main
and asserting: every line ≤ soft target (allowing the single-long-word case),
line count ≤ N, no mid-word splits. Then build in-tree and confirm no flicker and a
hard 2-line cap with live captions in OBS.

---

## LAYER 2 — Properties wiring

In `transcription-filter-properties.cpp` + `transcription-filter-data.h` +
locale ini, expose:

- `caption_soft_target` (int slider, default 35) → buffer `numPerSentence`.
- `caption_max_lines` (int slider, default 2) → buffer `numSentences`.
- `caption_decay_seconds` (int slider, default 3) → buffer `maxTime`
  (`std::chrono::seconds`). **Replace the hard-coded `3`** at the
  `captions_monitor.initialize(...)` site (~`transcription-filter.cpp:362`) and add
  a live-update path in the "parameters changed" branch just below it.
- `caption_label_text` (text) + `caption_label_enabled` (bool) → buffer label.

Read these in `transcription_filter_update()`, store on `gf`, push into the buffer
via its setters (add `setMaxTime`, `setLabel` if missing). Keep buffered output
always on (the new engine *is* the buffer); you can hide the old buffer-type
dropdown.

---

## LAYER 3 — Multi-source mix (Option A: mix → one whisper instance)

Goal: one filter instance transcribes the summed audio of several selected sources,
saving resources. Static group label (from Layer 1d); **no per-speaker
attribution** (acceptable and intended).

Approach — keep it simple and within the audio-filter model:
- Add a settings property `mix_extra_sources` (an editable list / multi-select of
  source names to mix in, in addition to the source the filter is attached to).
- In `transcription_filter_filter_audio()` (~line 116), in addition to the host
  source's frames, pull the most recent audio from each selected extra source and
  **sum** (mix) it sample-aligned into the same `input_buffers` before the existing
  resample/VAD path. Use OBS audio APIs to obtain the extra sources' samples; if
  direct pull is awkward, attach a lightweight capture callback
  (`obs_source_add_audio_capture_callback`) per selected source that feeds a small
  ring buffer this function drains. Keep channel/sample-rate handling consistent
  with the existing resample to whisper's 16 kHz mono.
- Guard against feedback/double-add if a user mixes a source that already has its
  own filter; document the expectation that grouped speakers should *not* each also
  run their own instance.

If true per-speaker labels are ever wanted later, that's a separate
"transcribe-separately, render-together" mode — out of scope now.

---

## LAYER 4 — Remote whisper.cpp server backend

Add a model-source choice in properties: **Local model** (existing) vs **Remote
whisper.cpp server**, with a `whisper_server_host` + `whisper_server_port` field
(show/hide like the existing `external_model_file_selection` toggler ~line 127).

Implement behind the inference seam. In `whisper-processing.cpp`, where
`run_whisper_inference()` is called (~line 360), branch: if remote mode, call a new
`run_remote_whisper_inference(gf, pcm32f_data, size, start_offset_ms)` that returns
the **same** `DetectionResultWithText` so everything downstream is unchanged.

Target the **bundled `whisper.cpp/server`** HTTP API:
- It accepts audio (WAV/PCM) via HTTP POST (multipart form, file field) to the
  transcription endpoint and returns JSON with transcribed text (and, depending on
  params, segments/timestamps).
- Reuse the project's existing libcurl helper
  (`src/translation/cloud-translation/curl-helper.{cpp,h}`) for the POST.
- Convert the float PCM buffer to a 16 kHz mono 16-bit WAV in memory, POST it, parse
  the JSON, and fill `text` + `start_timestamp_ms`/`end_timestamp_ms`
  (offset by `start_offset_ms`). If the server response lacks per-segment
  timestamps, synthesize them from the chunk's offset and duration.
- Fail soft: on connection error, log and return an empty result (don't crash the
  loop); surface a status note in properties if practical.

Note in the property description that the user must run `whisper.cpp/server`
themselves (e.g. `./server -m model.bin --host 0.0.0.0 --port 8080`) on the target
PC, and that host:port points at it.

---

## LAYER 5 — Sentence-buffered file output + SRT-per-recording

On-screen captions remain word-by-word (Layer 1). **File writing flushes only on
sentence boundaries** to preserve context.

- Maintain a separate **context buffer** of finalized text (default ~50 words / 2
  sentences, exposed as `file_context_words`, user-adjustable). Append finalized
  whisper text to it. When sentence-ending punctuation (`.`/`?`/`!`, with basic
  guarding) closes a sentence, flush *complete sentences* to file; keep the
  trailing partial sentence in the buffer for context.
- Replace the per-line trigger in `send_sentence_to_file()` usage with this
  sentence-boundary trigger. Keep `.txt` (plain, optionally truncating) and `.srt`
  as **independently toggleable** outputs (`save_txt`, `save_srt` booleans + two
  path settings). Offer saving `.srt` separately from `.txt`.
- **SRT-per-recording**: add `auto_srt_with_recording` (bool). In
  `recording_state_callback()` (~line 658):
  - On `OBS_FRONTEND_EVENT_RECORDING_STARTING`: if enabled, open a fresh `.srt`
    whose path is derived from the recording's output filename (get it via the
    recording output / `obs_frontend_get_last_recording()` on stop; on start you may
    need to capture the configured path), reset `sentence_number`, and record a
    `recording_start_ts` so SRT timecodes are **zeroed to recording start**.
  - While recording: write SRT cues using
    `(start_timestamp_ms - recording_start_ts)` → `(end_timestamp_ms -
    recording_start_ts)` formatted `HH:MM:SS,mmm`, incrementing `sentence_number`.
  - On `OBS_FRONTEND_EVENT_RECORDING_STOPPED`: finalize/close, and ensure the `.srt`
    filename matches the final recording filename (rename if OBS only reveals it on
    stop).
- SRT/streaming writing only active while recording or streaming (reuse the
  existing `save_only_while_recording`-style guard).

**Verify Layer 5** with a standalone test of the SRT timecode formatter and the
sentence-flush boundary logic before wiring to OBS events.

---

## LAYER 6 — Active-scene caption dock

A frontend dock (`obs_frontend_add_dock_by_id`, a `QDockWidget` built with
`obs_frontend_get_main_window()` as parent) that previews captions from filters on
sources **in the current scene only**, showing text identical to what's burned
on-screen.

- Maintain a small global registry of active caption-filter instances (each
  registers/unregisters itself on create/destroy) exposing: its host source, its
  current label, and its **current emitted caption string** (the buffer's last
  emitted text — have Layer 1's emit also update a shared per-instance snapshot).
- On `OBS_FRONTEND_EVENT_SCENE_CHANGED` (and periodically), enumerate the current
  scene's sources (`obs_frontend_get_current_scene()` →
  `obs_scene_enum_items`). For each registered filter whose host source is in that
  scene, render a row: `[label] current caption`. Sources not in the active scene
  are omitted. Blank when none.
- The dock is **read-only preview**; it must show the *same* string the buffer
  emitted to the text source (no re-formatting), so pull the cached emitted string,
  don't recompute.

---

## Cross-cutting requirements

- **Locale**: every new property/label gets a key in `data/locale/en-US.ini` and
  `en-GB.ini` using the existing `MT_(...)` convention.
- **Settings persistence**: add defaults in the filter's `get_defaults` and read in
  `update`. Don't break loading of existing user settings.
- **Thread safety**: the caption buffer runs its own thread; guard shared strings
  with the existing mutexes. The dock reads cached snapshots on the UI thread — copy
  under lock, never hold a lock across a Qt call.
- **Licensing**: localvocal and obs-captions-plugin are both **GPL-2.0**. The
  replace-filter logic and wrap algorithm are adapted from obs-captions-plugin;
  preserve GPL headers and add attribution in source comments and the README. The
  result must stay GPL-2.0.
- **Don't regress the engine**: never edit whisper/VAD internals; only branch at the
  named seams. After each layer, confirm the stock transcription path still works
  with a local model.

---

## Suggested commit sequence

1. `layer1: word-emit caption buffer with fuzzy wrap + decay + replace + label`
2. `layer2: expose soft-target/lines/decay/label properties`
3. `layer3: multi-source audio mix (Option A) with group label`
4. `layer4: remote whisper.cpp server inference backend`
5. `layer5: sentence-buffered txt/srt output + srt-per-recording`
6. `layer6: active-scene caption preview dock`

Build, load in OBS, and smoke-test after **each** commit. Ask for clarification
before guessing on anything ambiguous; prefer broadcast-correct defaults
(35-char soft target, 2-line hard cap, ~180 ms poll, decay in seconds) when a
detail is unspecified.
