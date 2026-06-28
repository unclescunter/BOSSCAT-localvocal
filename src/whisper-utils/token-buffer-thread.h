// This file is part of BOSSCAT-localvocal, a GPL-2.0 fork of obs-localvocal
// by Roy Shilkrot (https://github.com/locaal-ai/obs-localvocal).
// Modifications (c) 2025 unclescunter, licensed GPL-2.0. See CHANGES.md.
#ifndef TOKEN_BUFFER_THREAD_H
#define TOKEN_BUFFER_THREAD_H

#include <atomic>
#include <chrono>
#include <thread>
#include <mutex>
#include <functional>
#include <string>
#include <vector>

#include <obs.h>
#include "plugin-support.h"

struct transcription_filter_data;

// Keep enum values for API compatibility with callers.
enum TokenBufferSegmentation { SEGMENTATION_WORD = 0, SEGMENTATION_TOKEN, SEGMENTATION_SENTENCE };
enum TokenBufferSpeed { SPEED_SLOW = 0, SPEED_NORMAL, SPEED_FAST };

typedef std::chrono::time_point<std::chrono::steady_clock> TokenBufferTimePoint;

inline std::chrono::time_point<std::chrono::steady_clock> get_time_point_from_ms(uint64_t ms)
{
	return std::chrono::time_point<std::chrono::steady_clock>(std::chrono::milliseconds(ms));
}

// Legacy structs kept for API compatibility with addSentence() callers.
struct TokenBufferToken {
	std::string token;
	bool is_partial;
};

struct TokenBufferSentence {
	std::vector<TokenBufferToken> tokens;
	TokenBufferTimePoint start_time;
	TokenBufferTimePoint end_time;
};

class TokenBufferThread {
public:
	TokenBufferThread() noexcept;
	~TokenBufferThread();

	void initialize(struct transcription_filter_data *gf,
			std::function<void(const std::string &)> captionPresentationCallback_,
			size_t numSentences_, size_t numTokensPerSentence_,
			std::chrono::seconds maxTime_,
			TokenBufferSegmentation segmentation_ = SEGMENTATION_WORD);

	// Primary entry point called by the transcription engine.
	void addSentenceFromStdString(const std::string &sentence, TokenBufferTimePoint start_time,
				      TokenBufferTimePoint end_time, bool is_partial = false);

	// Legacy entry point — joins tokens and delegates to addSentenceFromStdString.
	void addSentence(const TokenBufferSentence &sentence);

	void clear();
	void stopThread();

	bool isEnabled() const { return !stop; }

	void setNumSentences(size_t n)
	{
		std::lock_guard<std::mutex> lock(dataMutex);
		numSentences = n;
	}
	void setNumPerSentence(size_t n)
	{
		std::lock_guard<std::mutex> lock(dataMutex);
		numPerSentence = n;
	}
	void setMaxTime(std::chrono::seconds t)
	{
		std::lock_guard<std::mutex> lock(dataMutex);
		maxTime = t;
	}
	// Kept for API compatibility; the buffer uses word-mode internally.
	void setSegmentation(TokenBufferSegmentation /*s*/) {}
	void setCaptionPresentationCallback(
		std::function<void(const std::string &)> cb)
	{
		std::lock_guard<std::mutex> lock(dataMutex);
		captionPresentationCallback = cb;
	}
	void setLabel(const std::string &text, bool enabled)
	{
		std::lock_guard<std::mutex> lock(dataMutex);
		label = text;
		labelEnabled = enabled;
	}

	// Current emitted caption (for dock preview).
	std::string getLastEmitted() const
	{
		std::lock_guard<std::mutex> lock(dataMutex);
		return lastEmitted;
	}

private:
	void monitor();
	std::string renderCaption();

	struct transcription_filter_data *gf;

	std::string workingFinal;   // committed final results, space-joined
	std::string workingPartial; // current refining partial tail

	std::string lastEmitted;

	std::string label;
	bool labelEnabled = false;

	std::function<void(const std::string &)> captionPresentationCallback;

	mutable std::mutex dataMutex;
	std::thread workerThread;

	std::chrono::seconds maxTime{0};
	std::atomic<bool> stop{true};
	bool newDataAvailable = false;

	size_t numSentences = 2;
	size_t numPerSentence = 35; // soft char target per line
	TokenBufferSegmentation segmentation = SEGMENTATION_WORD; // compat only

	TokenBufferTimePoint lastContributionTime;
	TokenBufferTimePoint lastCaptionTime;
};

#endif // TOKEN_BUFFER_THREAD_H
