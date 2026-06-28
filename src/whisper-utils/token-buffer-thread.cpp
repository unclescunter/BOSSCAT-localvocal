// This file is part of BOSSCAT-localvocal, a GPL-2.0 fork of obs-localvocal
// by Roy Shilkrot (https://github.com/locaal-ai/obs-localvocal).
// Modifications (c) 2025 unclescunter, licensed GPL-2.0. See CHANGES.md.
//
// Word-wrap algorithm adapted from obs-captions-plugin by "RatWithAShotgun"
// (GPL-2.0, https://github.com/obsproject/obs-captions-plugin).

#include "token-buffer-thread.h"
#include "transcription-filter-data.h"

#include <obs-module.h>

#include <sstream>
#include <algorithm>
#include <regex>

// ---------------------------------------------------------------------------
// Fuzzy greedy word-wrap (soft char target).  Lines never exceed soft_target
// unless a single word is longer — that case is hard-split character by char.
// Adapted from obs-captions-plugin (RatWithAShotgun, GPL-2.0).
// ---------------------------------------------------------------------------
static std::vector<std::string> wrap_to_lines(const std::string &text, size_t soft_target)
{
	std::vector<std::string> lines;
	if (soft_target == 0) {
		lines.push_back(text);
		return lines;
	}
	std::istringstream iss(text);
	std::string word, line;
	auto flush = [&]() {
		lines.push_back(line);
		line.clear();
	};
	while (iss >> word) {
		const size_t projected = line.size() + (line.empty() ? 0 : 1) + word.size();
		if (projected <= soft_target) {
			if (!line.empty())
				line += " ";
			line += word;
		} else if (word.size() > soft_target) {
			// word longer than a full line — hard-split it
			if (!line.empty()) {
				if (line.size() + 2 <= soft_target)
					line += " ";
				else
					flush();
			}
			for (char c : word) {
				if (line.size() + 1 <= soft_target)
					line += c;
				else {
					flush();
					line += c;
				}
			}
		} else {
			if (!line.empty())
				flush();
			line = word;
		}
	}
	if (!line.empty())
		lines.push_back(line);
	return lines;
}

// Apply the filter_words_replace list (regex icase replace) to text.
static std::string apply_replacements(
	const std::string &text,
	const std::vector<std::tuple<std::string, std::string>> &replacements)
{
	std::string out = text;
	for (const auto &r : replacements) {
		try {
			out = std::regex_replace(
				out,
				std::regex(std::get<0>(r), std::regex_constants::icase),
				std::get<1>(r));
		} catch (...) {
		}
	}
	return out;
}

// ---------------------------------------------------------------------------
// TokenBufferThread
// ---------------------------------------------------------------------------

TokenBufferThread::TokenBufferThread() noexcept : gf(nullptr), stop(true) {}

TokenBufferThread::~TokenBufferThread()
{
	stopThread();
}

void TokenBufferThread::initialize(struct transcription_filter_data *gf_,
				   std::function<void(const std::string &)> cb,
				   size_t numSentences_, size_t numPerSentence_,
				   std::chrono::seconds maxTime_,
				   TokenBufferSegmentation /*segmentation_*/)
{
	gf = gf_;
	{
		std::lock_guard<std::mutex> lock(dataMutex);
		captionPresentationCallback = cb;
		numSentences = numSentences_;
		numPerSentence = numPerSentence_;
		maxTime = maxTime_;
		workingFinal.clear();
		workingPartial.clear();
		lastEmitted.clear();
		newDataAvailable = false;
	}
	stop = false;
	lastContributionTime = std::chrono::steady_clock::now();
	lastCaptionTime = std::chrono::steady_clock::now();
	workerThread = std::thread(&TokenBufferThread::monitor, this);
}

void TokenBufferThread::stopThread()
{
	stop = true;
	if (workerThread.joinable())
		workerThread.join();
}

void TokenBufferThread::addSentenceFromStdString(const std::string &sentence,
						 TokenBufferTimePoint /*start*/,
						 TokenBufferTimePoint /*end*/,
						 bool is_partial)
{
	if (sentence.empty())
		return;

	// Collapse embedded newlines to spaces.
	std::string flat = sentence;
	std::replace(flat.begin(), flat.end(), '\n', ' ');
	std::replace(flat.begin(), flat.end(), '\r', ' ');

	{
		std::lock_guard<std::mutex> lock(dataMutex);
		if (is_partial) {
			workingPartial = flat;
		} else {
			if (!workingFinal.empty())
				workingFinal += " ";
			workingFinal += flat;
			workingPartial.clear();
		}
		lastContributionTime = std::chrono::steady_clock::now();
		newDataAvailable = true;
	}
}

void TokenBufferThread::addSentence(const TokenBufferSentence &sentence)
{
	// Reconstruct plain string from token list and delegate.
	std::string text;
	for (const auto &tok : sentence.tokens) {
		if (!text.empty() && tok.token != " ")
			text += " ";
		text += tok.token;
	}
	bool is_partial = !sentence.tokens.empty() && sentence.tokens.back().is_partial;
	addSentenceFromStdString(text, sentence.start_time, sentence.end_time, is_partial);
}

void TokenBufferThread::clear()
{
	std::function<void(const std::string &)> cb;
	{
		std::lock_guard<std::mutex> lock(dataMutex);
		workingFinal.clear();
		workingPartial.clear();
		lastEmitted.clear();
		newDataAvailable = false;
		cb = captionPresentationCallback;
		lastCaptionTime = std::chrono::steady_clock::now();
	}
	if (cb)
		cb("");
}

// Build the formatted caption string from current working text.
// Must be called with dataMutex held.
std::string TokenBufferThread::renderCaption()
{
	std::string working;
	if (!workingFinal.empty()) {
		working = workingFinal;
		if (!workingPartial.empty())
			working += " " + workingPartial;
	} else {
		working = workingPartial;
	}

	if (working.empty())
		return "";

	// Apply word replacements.
	if (gf && !gf->filter_words_replace.empty())
		working = apply_replacements(working, gf->filter_words_replace);

	// Prepend label if enabled.
	if (labelEnabled && !label.empty())
		working = label + ": " + working;

	// Wrap to lines using soft target.
	auto lines = wrap_to_lines(working, numPerSentence);

	// Hard cap: keep only the last numSentences lines.
	if (lines.size() > numSentences)
		lines.erase(lines.begin(), lines.begin() + (ptrdiff_t)(lines.size() - numSentences));

	// Join with newlines.
	std::string out;
	for (size_t i = 0; i < lines.size(); ++i) {
		if (i > 0)
			out += "\n";
		out += lines[i];
	}
	return out;
}

void TokenBufferThread::monitor()
{
	obs_log(LOG_INFO, "TokenBufferThread::monitor start");

	{
		std::lock_guard<std::mutex> lock(dataMutex);
		if (captionPresentationCallback)
			captionPresentationCallback("");
	}

	while (!stop) {
		std::this_thread::sleep_for(std::chrono::milliseconds(180));

		if (stop)
			break;

		std::function<void(const std::string &)> cb;
		std::string toEmit;
		bool shouldClear = false;

		{
			std::lock_guard<std::mutex> lock(dataMutex);
			cb = captionPresentationCallback;

			if (newDataAvailable) {
				newDataAvailable = false;
				toEmit = renderCaption();
				if (toEmit != lastEmitted) {
					lastEmitted = toEmit;
					lastCaptionTime = std::chrono::steady_clock::now();
				} else {
					toEmit.clear(); // no change — skip emit
				}
			} else if (maxTime.count() > 0 && !lastEmitted.empty()) {
				const auto now = std::chrono::steady_clock::now();
				const auto elapsed =
					std::chrono::duration_cast<std::chrono::seconds>(
						now - lastCaptionTime);
				if (elapsed >= maxTime) {
					shouldClear = true;
					lastEmitted.clear();
					workingFinal.clear();
					workingPartial.clear();
				}
			}
		}

		// Call the callback without holding the lock.
		if (shouldClear && cb)
			cb("");
		else if (!toEmit.empty() && cb)
			cb(toEmit);
	}

	obs_log(LOG_INFO, "TokenBufferThread::monitor done");
}
