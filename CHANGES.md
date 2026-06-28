# CHANGES — BOSSCAT-localvocal

This project is a fork of **obs-localvocal** by Roy Shilkrot
(<https://github.com/locaal-ai/obs-localvocal>), licensed GNU GPL v2.

Upstream commit this fork was based on is recorded in `.forked-from-commit.txt`.

Modifications © 2025 unclescunter, licensed GNU GPL v2. See `LICENSE`.

---

## Fork differences from upstream

### Layer 1 — Caption engine: fuzzy wrap + per-word emit + decay + replace + label

Rewrote `src/whisper-utils/token-buffer-thread.cpp`:

- Replaced character-streaming queue model with a two-string model (`workingFinal`
  + `workingPartial`) that emits only when the formatted output changes → eliminates
  flicker and guarantees words are never split across lines spuriously.
- Implemented greedy "fuzzy" word-wrap (`wrap_to_lines`) with a soft character
  target (default 35) adapted from the obs-captions-plugin algorithm by
  "RatWithAShotgun" (GPL-2.0, <https://github.com/obsproject/obs-captions-plugin>).
- Monitor thread polls at 180 ms; clears display after configurable decay seconds.
- Replace hook applied to the full working string before wrapping, reusing the
  existing `filter_words_replace` list.
- Label prefix: optional `label: ` prepended to the first line before wrapping.

### Layer 2 — Properties wiring

Exposed `caption_soft_target`, `caption_max_lines`, `caption_decay_seconds`,
`caption_label_text`, and `caption_label_enabled` in the filter properties UI and
locale files. Replaced the hard-coded `std::chrono::seconds(3)` decay.

### Layer 3 — Multi-source audio mix

Added `mix_extra_sources` list property. At the audio-filter callback seam,
additional source audio is captured via `obs_source_add_audio_capture_callback` into
per-source ring buffers and summed into the main `input_buffers` before the existing
resample/VAD path.

### Layer 4 — Remote whisper.cpp server backend

Added an inference-backend selector in properties. When "Remote server" is chosen, a
new `run_remote_whisper_inference()` function POSTs 16 kHz mono PCM as a WAV to a
user-supplied `whisper.cpp/server` HTTP endpoint, parses the JSON response, and
returns the same `DetectionResultWithText` used by the local path. Uses the
project's existing libcurl helper.

### Layer 5 — Sentence-buffered file output + SRT-per-recording

- File writes now flush only at sentence boundaries (`.`/`?`/`!`) to preserve
  context; `.txt` and `.srt` are independently toggleable.
- `auto_srt_with_recording`: opens a fresh `.srt` file on recording start, zeroes
  timecodes to `recording_start_ts`, and renames to match the final recording
  filename on stop.

### Layer 6 — Active-scene caption dock

A read-only `QDockWidget` previewing captions from filters on sources in the current
OBS scene. Powered by a global filter registry; updates on
`OBS_FRONTEND_EVENT_SCENE_CHANGED`.

---

## Attribution

- **obs-localvocal** (Roy Shilkrot, GPL-2.0): base project, all transcription engine
  code (whisper.cpp, Silero VAD, ONNX Runtime integration).
- **obs-captions-plugin** ("RatWithAShotgun", GPL-2.0): word-wrap algorithm and
  regex replace-filter concept adapted for the Layer 1 caption buffer.
