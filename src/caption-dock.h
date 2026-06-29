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
	std::string text_source_name; // OBS text source to clear on mute
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

// Called once from transcription-filter.cpp after obs_module_load.
void caption_dock_init();

// Called on obs_module_unload.
void caption_dock_shutdown();
