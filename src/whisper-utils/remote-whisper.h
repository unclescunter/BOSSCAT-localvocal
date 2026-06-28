// This file is part of BOSSCAT-localvocal, a GPL-2.0 fork of obs-localvocal
// by Roy Shilkrot (https://github.com/locaal-ai/obs-localvocal).
// Modifications (c) 2025 unclescunter, licensed GPL-2.0. See CHANGES.md.
#pragma once

#include "whisper-processing.h"

struct transcription_filter_data;

// POST a 16 kHz mono float PCM buffer to a whisper.cpp/server HTTP endpoint.
// Returns a DetectionResultWithText with the same layout as run_whisper_inference().
// On connection error, logs and returns an empty DETECTION_RESULT_UNKNOWN result.
DetectionResultWithText run_remote_whisper_inference(struct transcription_filter_data *gf,
						     const float *pcm32f_data, size_t pcm32f_size,
						     uint64_t start_offset_ms,
						     uint64_t end_offset_ms);
