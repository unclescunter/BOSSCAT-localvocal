// This file is part of BOSSCAT-localvocal, a GPL-2.0 fork of obs-localvocal
// by Roy Shilkrot (https://github.com/locaal-ai/obs-localvocal).
// Modifications (c) 2025 unclescunter, licensed GPL-2.0. See CHANGES.md.
#pragma once

#include <string>
#include <mutex>
#include <vector>

struct transcription_filter_data;

// ---------------------------------------------------------------------------
// Global filter registry — each active filter instance registers here so the
// dock can display the latest caption without holding a gf pointer after
// the lock is released.
// ---------------------------------------------------------------------------

struct CaptionFilterEntry {
	transcription_filter_data *gf; // identity only — never deref outside registry lock
	std::string host_source_name;
	std::string last_caption;    // latest text, written under g_registry_mutex
	std::string label;
	bool label_enabled = false;
	std::vector<std::string> text_source_names; // OBS text sources to clear on mute
};

// Register / unregister called from transcription_filter_create / _destroy.
void caption_registry_add(transcription_filter_data *gf,
			  const std::string &host_source_name);
void caption_registry_remove(transcription_filter_data *gf);

// Called from output_text() (NO_TRANSLATION path) to push the latest caption
// into the registry so the dock can read it safely.
void caption_dock_update(transcription_filter_data *gf, const std::string &caption,
			 const std::string &label, bool label_enabled);

// Returns true when the user has muted subtitles from the dock.
// Checked by send_caption_to_source to suppress output without stopping whisper.
bool caption_dock_is_muted();

// ---------------------------------------------------------------------------
// BOSSCAT — combined / per-source SRT session.
// A single global session collects finalized sentence cues from every filter
// while a recording or stream is active, then on stop writes:
//   * one combined .srt (all opted-in sources, each line "Label: text"), and
//   * one .srt per opted-in source ("<base> <label>.srt").
// Files are named after the recording (YYYY-MM-DD HH-MM-SS) or, for a
// stream, after the stream-start timestamp.
// ---------------------------------------------------------------------------

// True while a recording or streaming session is collecting cues.
bool srt_session_active();

// Add a finalized sentence cue. Timestamps are epoch-ms (as in now_ms()); the
// session zeroes them against the session start. label_text/label_enabled
// mirror the dock's labelling; when the label is disabled the source's host
// name is used. combined/per_source are the per-filter opt-in toggles.
// lang_code is empty for original transcription cues, or a BCP 47 language
// subtag (e.g. "es", "en") for translated cues — translated cues are written
// to separate files with the code appended to the name.
void srt_session_add_cue(const void *source_key, uint64_t start_ms, uint64_t end_ms,
			 const std::string &text, const std::string &label_text,
			 bool label_enabled, bool combined, bool per_source,
			 const std::string &output_dir, const std::string &lang_code);

// Called once from transcription-filter.cpp after obs_module_load.
void caption_dock_init();

// Called on obs_module_unload.
void caption_dock_shutdown();
