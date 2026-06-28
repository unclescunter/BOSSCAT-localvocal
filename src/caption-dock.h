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
// dock can find the current emitted caption without re-formatting.
// ---------------------------------------------------------------------------

struct CaptionFilterEntry {
	transcription_filter_data *gf;
	std::string host_source_name; // obs_source_get_name(obs_filter_get_parent())
};

// Register / unregister called from transcription_filter_create / _destroy.
void caption_registry_add(transcription_filter_data *gf,
			  const std::string &host_source_name);
void caption_registry_remove(transcription_filter_data *gf);

// Called once from transcription-filter.cpp after obs_module_load.
void caption_dock_init();

// Called on obs_module_unload.
void caption_dock_shutdown();
