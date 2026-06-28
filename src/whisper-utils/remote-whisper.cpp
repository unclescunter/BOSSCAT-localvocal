// This file is part of BOSSCAT-localvocal, a GPL-2.0 fork of obs-localvocal
// by Roy Shilkrot (https://github.com/locaal-ai/obs-localvocal).
// Modifications (c) 2025 unclescunter, licensed GPL-2.0. See CHANGES.md.

#include "remote-whisper.h"
#include "transcription-filter-data.h"
#include "plugin-support.h"

#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <obs-module.h>

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <sstream>

// ---------------------------------------------------------------------------
// Build a minimal 16 kHz mono 16-bit PCM WAV in memory.
// ---------------------------------------------------------------------------
static std::vector<uint8_t> build_wav(const float *pcm32f, size_t num_samples)
{
	const uint32_t sample_rate = 16000;
	const uint16_t channels = 1;
	const uint16_t bits = 16;
	const uint32_t data_bytes = (uint32_t)(num_samples * channels * (bits / 8));
	const uint32_t riff_size = 36 + data_bytes;

	std::vector<uint8_t> wav(44 + data_bytes);
	uint8_t *p = wav.data();

	auto write16 = [](uint8_t *d, uint16_t v) {
		d[0] = (uint8_t)(v & 0xFFu);
		d[1] = (uint8_t)((v >> 8u) & 0xFFu);
	};
	auto write32 = [](uint8_t *d, uint32_t v) {
		d[0] = (uint8_t)(v & 0xFFu);
		d[1] = (uint8_t)((v >> 8u) & 0xFFu);
		d[2] = (uint8_t)((v >> 16u) & 0xFFu);
		d[3] = (uint8_t)((v >> 24u) & 0xFFu);
	};

	// RIFF header
	memcpy(p, "RIFF", 4);
	write32(p + 4, riff_size);
	memcpy(p + 8, "WAVE", 4);
	// fmt chunk
	memcpy(p + 12, "fmt ", 4);
	write32(p + 16, 16); // chunk size
	write16(p + 20, 1);  // PCM
	write16(p + 22, channels);
	write32(p + 24, sample_rate);
	write32(p + 28, sample_rate * channels * (bits / 8)); // byte rate
	write16(p + 32, (uint16_t)(channels * (bits / 8)));  // block align
	write16(p + 34, bits);
	// data chunk
	memcpy(p + 36, "data", 4);
	write32(p + 40, data_bytes);

	// Convert float → int16
	int16_t *samples = reinterpret_cast<int16_t *>(p + 44);
	for (size_t i = 0; i < num_samples; i++) {
		float f = pcm32f[i];
		if (f > 1.0f) f = 1.0f;
		if (f < -1.0f) f = -1.0f;
		samples[i] = (int16_t)(f * 32767.0f);
	}

	return wav;
}

// ---------------------------------------------------------------------------
// Curl write callback: appends received bytes to a std::string.
// ---------------------------------------------------------------------------
static size_t curl_write_cb(void *ptr, size_t size, size_t nmemb, void *userdata)
{
	auto *buf = static_cast<std::string *>(userdata);
	buf->append(static_cast<char *>(ptr), size * nmemb);
	return size * nmemb;
}

// ---------------------------------------------------------------------------
// Main remote inference function.
// ---------------------------------------------------------------------------
DetectionResultWithText run_remote_whisper_inference(struct transcription_filter_data *gf,
						     const float *pcm32f_data, size_t pcm32f_size,
						     uint64_t start_offset_ms,
						     uint64_t end_offset_ms)
{
	DetectionResultWithText result;
	result.result = DETECTION_RESULT_UNKNOWN;
	result.start_timestamp_ms = start_offset_ms;
	result.end_timestamp_ms = end_offset_ms;

	if (!pcm32f_data || pcm32f_size == 0) {
		obs_log(LOG_WARNING, "remote_whisper: empty audio buffer");
		return result;
	}

	const std::string host = gf->whisper_server_host.empty() ? "127.0.0.1"
								 : gf->whisper_server_host;
	const int port = gf->whisper_server_port > 0 ? gf->whisper_server_port : 8080;

	std::string url = "http://" + host + ":" + std::to_string(port) + "/inference";

	// Build WAV in memory.
	auto wav_bytes = build_wav(pcm32f_data, pcm32f_size);

	CURL *curl = curl_easy_init();
	if (!curl) {
		obs_log(LOG_ERROR, "remote_whisper: curl_easy_init failed");
		return result;
	}

	std::string response_body;
	CURLcode res = CURLE_OK;

	struct curl_httppost *form = nullptr;
	struct curl_httppost *last = nullptr;
	curl_formadd(&form, &last,
		     CURLFORM_COPYNAME, "file",
		     CURLFORM_BUFFER, "audio.wav",
		     CURLFORM_BUFFERPTR, wav_bytes.data(),
		     CURLFORM_BUFFERLENGTH, (long)wav_bytes.size(),
		     CURLFORM_CONTENTTYPE, "audio/wav",
		     CURLFORM_END);

	curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
	curl_easy_setopt(curl, CURLOPT_HTTPPOST, form);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_cb);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_body);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);

	res = curl_easy_perform(curl);
	curl_formfree(form);
	curl_easy_cleanup(curl);

	if (res != CURLE_OK) {
		obs_log(LOG_ERROR, "remote_whisper: request to %s failed: %s", url.c_str(),
			curl_easy_strerror(res));
		return result;
	}

	// Parse JSON response from whisper.cpp server.
	try {
		auto j = nlohmann::json::parse(response_body);
		if (j.contains("text") && j["text"].is_string()) {
			result.text = j["text"].get<std::string>();
			// Trim leading/trailing whitespace.
			size_t s0 = result.text.find_first_not_of(" \t\n\r");
			if (s0 == std::string::npos) {
				result.text.clear();
			} else {
				size_t s1 = result.text.find_last_not_of(" \t\n\r");
				result.text = result.text.substr(s0, s1 - s0 + 1);
			}
		}
		// Use per-segment timestamps if present, otherwise use chunk offsets.
		if (j.contains("segments") && j["segments"].is_array() &&
		    !j["segments"].empty()) {
			const auto &segs = j["segments"];
			double seg_start = segs.front().value("start", 0.0);
			double seg_end = segs.back().value("end", 0.0);
			result.start_timestamp_ms =
				start_offset_ms + (uint64_t)(seg_start * 1000.0);
			result.end_timestamp_ms =
				start_offset_ms + (uint64_t)(seg_end * 1000.0);
		}
		if (!result.text.empty()) {
			result.result = DETECTION_RESULT_SPEECH;
		} else {
			result.result = DETECTION_RESULT_SILENCE;
		}
	} catch (const std::exception &e) {
		obs_log(LOG_ERROR, "remote_whisper: JSON parse error: %s", e.what());
		obs_log(LOG_DEBUG, "remote_whisper: response body: %s", response_body.c_str());
	}

	return result;
}
