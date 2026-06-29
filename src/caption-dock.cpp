// This file is part of BOSSCAT-localvocal, a GPL-2.0 fork of obs-localvocal
// by Roy Shilkrot (https://github.com/locaal-ai/obs-localvocal).
// Modifications (c) 2025 unclescunter, licensed GPL-2.0. See CHANGES.md.

#include "caption-dock.h"
#include "transcription-filter-data.h"
#include "transcription-utils.h"
#include "plugin-support.h"

#include <obs-module.h>
#include <obs-frontend-api.h>

#include <QDockWidget>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QWidget>
#include <QPointer>
#include <QTimer>
#include <QString>
#include <QMainWindow>

#include <atomic>
#include <mutex>
#include <vector>
#include <string>
#include <algorithm>
#include <map>
#include <set>
#include <fstream>
#include <filesystem>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

static std::atomic<bool> g_subtitles_muted{false};

bool caption_dock_is_muted()
{
	return g_subtitles_muted.load(std::memory_order_relaxed);
}

// ---------------------------------------------------------------------------
// Global filter registry
// ---------------------------------------------------------------------------
static std::mutex g_registry_mutex;
static std::vector<CaptionFilterEntry> g_registry;

void caption_registry_add(transcription_filter_data *gf, const std::string &host_source_name)
{
	std::lock_guard<std::mutex> lock(g_registry_mutex);
	g_registry.erase(std::remove_if(g_registry.begin(), g_registry.end(),
					[gf](const CaptionFilterEntry &e) { return e.gf == gf; }),
			 g_registry.end());
	CaptionFilterEntry entry;
	entry.gf = gf;
	entry.host_source_name = host_source_name;
	g_registry.push_back(std::move(entry));
}

void caption_registry_remove(transcription_filter_data *gf)
{
	std::lock_guard<std::mutex> lock(g_registry_mutex);
	g_registry.erase(std::remove_if(g_registry.begin(), g_registry.end(),
					[gf](const CaptionFilterEntry &e) { return e.gf == gf; }),
			 g_registry.end());
}

void caption_dock_update(transcription_filter_data *gf, const std::string &caption,
			 const std::string &label, bool label_enabled)
{
	std::lock_guard<std::mutex> lock(g_registry_mutex);
	for (auto &entry : g_registry) {
		if (entry.gf == gf) {
			entry.last_caption = caption;
			entry.label = label;
			entry.label_enabled = label_enabled;
			entry.text_source_names = gf->text_source_names;
			break;
		}
	}
}

// ---------------------------------------------------------------------------
// Snapshot helpers
// ---------------------------------------------------------------------------
static std::vector<std::pair<std::string, std::string>> get_all_captions()
{
	std::vector<std::pair<std::string, std::string>> out;
	std::lock_guard<std::mutex> lock(g_registry_mutex);
	for (const auto &entry : g_registry) {
		std::string label =
			entry.label_enabled ? entry.label : entry.host_source_name;
		out.push_back({label, entry.last_caption});
	}
	return out;
}

// Clears all known text sources (called once when muting so the last subtitle
// doesn't linger on screen).
static void clear_all_text_sources()
{
	std::vector<std::string> to_clear;
	{
		std::lock_guard<std::mutex> lock(g_registry_mutex);
		for (const auto &entry : g_registry)
			for (const auto &name : entry.text_source_names)
				if (!name.empty())
					to_clear.push_back(name);
	}
	for (const auto &name : to_clear) {
		obs_source_t *src = obs_get_source_by_name(name.c_str());
		if (!src)
			continue;
		obs_data_t *settings = obs_source_get_settings(src);
		obs_data_set_string(settings, "text", "");
		obs_source_update(src, settings);
		obs_data_release(settings);
		obs_source_release(src);
	}
}

// ---------------------------------------------------------------------------
// Dock content widget
// ---------------------------------------------------------------------------
class CaptionContentWidget : public QWidget {
public:
	explicit CaptionContentWidget(QWidget *parent = nullptr) : QWidget(parent)
	{
		auto *layout = new QVBoxLayout(this);
		layout->setContentsMargins(4, 4, 4, 4);
		layout->setSpacing(6);

		captionLabel = new QLabel(this);
		captionLabel->setWordWrap(true);
		captionLabel->setAlignment(Qt::AlignTop | Qt::AlignLeft);
		captionLabel->setTextFormat(Qt::PlainText);
		captionLabel->setStyleSheet("QLabel { font-size: 13pt; }");
		captionLabel->setText("(no LocalVocal-Bosscat-Flavour filters active)");
		captionLabel->setMinimumHeight(60);

		muteButton = new QPushButton(this);
		muteButton->setCheckable(true);
		muteButton->setChecked(false);
		updateMuteButtonLabel(false);
		connect(muteButton, &QPushButton::toggled, this,
			&CaptionContentWidget::onMuteToggled);

		layout->addWidget(captionLabel, 1);
		layout->addWidget(muteButton, 0);

		refreshTimer = new QTimer(this);
		connect(refreshTimer, &QTimer::timeout, this,
			&CaptionContentWidget::refreshCaptions);
		refreshTimer->start(200);
	}

	void refreshCaptions()
	{
		if (g_subtitles_muted.load(std::memory_order_relaxed))
			return; // label already set to "(subtitles muted)"
		auto rows = get_all_captions();
		if (rows.empty()) {
			captionLabel->setText("(no LocalVocal-Bosscat-Flavour filters active)");
			return;
		}
		QString text;
		for (const auto &row : rows) {
			if (!text.isEmpty())
				text += "\n\n";
			if (!row.first.empty())
				text += QString::fromStdString(row.first) + ": ";
			text += row.second.empty() ? tr("(listening...)")
						   : QString::fromStdString(row.second);
		}
		captionLabel->setText(text);
	}

private slots:
	void onMuteToggled(bool muted)
	{
		g_subtitles_muted.store(muted, std::memory_order_relaxed);
		updateMuteButtonLabel(muted);
		if (muted) {
			clear_all_text_sources();
			captionLabel->setText("(subtitles muted)");
		}
	}

private:
	void updateMuteButtonLabel(bool muted)
	{
		muteButton->setText(muted ? tr("Unmute Subtitles") : tr("Mute Subtitles"));
	}

	QLabel *captionLabel = nullptr;
	QPushButton *muteButton = nullptr;
	QTimer *refreshTimer = nullptr;
};

// ---------------------------------------------------------------------------
// BOSSCAT — combined / per-source SRT session
// ---------------------------------------------------------------------------
namespace {

struct SrtCue {
	const void *source;     // filter identity, for grouping per-source files
	std::string label;      // resolved display label (e.g. "Speaker1")
	std::string text;       // sentence text (no prefix)
	uint64_t start_ms;      // relative to session start
	uint64_t end_ms;        // relative to session start
	bool combined;          // include in the combined file
	bool per_source;        // write to this source's own file
	std::string output_dir; // configured directory ("" = next to recording)
	std::string lang_code;  // "" = original transcription; else BCP 47 (translated)
};

std::mutex g_srt_mutex;
bool g_srt_recording = false;
bool g_srt_streaming = false;
bool g_srt_active = false;
uint64_t g_srt_start_ms = 0;
std::vector<SrtCue> g_srt_cues;
std::string g_srt_last_rec_path; // captured at RECORDING_STOPPED

std::string sanitize_for_filename(const std::string &in)
{
	std::string out = in;
	for (char &c : out) {
		if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' ||
		    c == '"' || c == '<' || c == '>' || c == '|' || c == ' ')
			c = '_';
	}
	while (!out.empty() && (out.back() == '_' || out.back() == '.'))
		out.pop_back();
	return out;
}

std::string format_srt_timestamp(uint64_t ms)
{
	uint64_t time_s = ms / 1000;
	uint64_t time_m = time_s / 60;
	uint64_t time_h = time_m / 60;
	char buf[32];
	snprintf(buf, sizeof(buf), "%02llu:%02llu:%02llu,%03llu",
		 (unsigned long long)(time_h), (unsigned long long)(time_m % 60),
		 (unsigned long long)(time_s % 60), (unsigned long long)(ms % 1000));
	return std::string(buf);
}

std::string format_session_basename(uint64_t epoch_ms)
{
	std::time_t t = (std::time_t)(epoch_ms / 1000);
	std::tm tm_buf;
	localtime_r(&t, &tm_buf);
	char buf[32];
	strftime(buf, sizeof(buf), "%Y-%m-%d %H-%M-%S", &tm_buf);
	return std::string(buf);
}

void write_srt_file(const std::string &path, const std::vector<const SrtCue *> &cues,
		    bool prefix_with_label)
{
	std::ofstream f(path, std::ios::out | std::ios::trunc);
	if (!f.is_open()) {
		obs_log(LOG_ERROR, "srt_session: cannot open %s", path.c_str());
		return;
	}
	int n = 1;
	for (const SrtCue *c : cues) {
		f << n++ << "\n";
		f << format_srt_timestamp(c->start_ms) << " --> "
		  << format_srt_timestamp(c->end_ms) << "\n";
		if (prefix_with_label && !c->label.empty())
			f << c->label << ": ";
		f << c->text << "\n\n";
	}
	f.close();
	obs_log(LOG_INFO, "srt_session: wrote %s", path.c_str());
}

// Resolve the directory for an output: configured dir wins, else recording
// directory, else the user's home (streaming with no configured directory).
std::string resolve_output_dir(const std::string &configured, const std::string &rec_dir)
{
	if (!configured.empty())
		return configured;
	if (!rec_dir.empty())
		return rec_dir;
	const char *home = getenv("HOME");
	return home ? std::string(home) : std::string(".");
}

// Called with g_srt_mutex held. Writes combined + per-source files and clears.
void srt_session_finalize_locked()
{
	namespace fs = std::filesystem;

	if (g_srt_cues.empty()) {
		obs_log(LOG_INFO, "srt_session: no cues, nothing to write");
		return;
	}

	std::string base;
	std::string rec_dir;
	if (!g_srt_last_rec_path.empty()) {
		fs::path recPath(g_srt_last_rec_path);
		base = recPath.stem().string();
		rec_dir = recPath.parent_path().string();
	} else {
		base = format_session_basename(g_srt_start_ms);
	}

	// Combined file: all opted-in cues matching `pred`, sorted by start time,
	// label-prefixed, written as "<base><suffix>.srt".
	auto write_combined = [&](auto pred, const std::string &suffix) {
		std::vector<const SrtCue *> cues;
		std::string dir_pick;
		for (const SrtCue &c : g_srt_cues) {
			if (!c.combined || !pred(c))
				continue;
			cues.push_back(&c);
			if (dir_pick.empty() && !c.output_dir.empty())
				dir_pick = c.output_dir;
		}
		if (cues.empty())
			return;
		std::stable_sort(cues.begin(), cues.end(),
				 [](const SrtCue *a, const SrtCue *b) {
					 return a->start_ms < b->start_ms;
				 });
		std::string dir = resolve_output_dir(dir_pick, rec_dir);
		write_srt_file((fs::path(dir) / (base + suffix + ".srt")).string(), cues, true);
	};

	// Per-source files: one per opted-in source for cues matching `pred`,
	// written as "<base> <label><suffix>.srt" (no line prefix — the filename
	// already identifies the speaker).
	auto write_per_source = [&](auto pred, const std::string &suffix) {
		std::map<const void *, std::vector<const SrtCue *>> groups;
		std::map<const void *, std::string> labels;
		std::map<const void *, std::string> dirs;
		for (const SrtCue &c : g_srt_cues) {
			if (!c.per_source || !pred(c))
				continue;
			groups[c.source].push_back(&c);
			labels[c.source] = c.label;
			if (!c.output_dir.empty())
				dirs[c.source] = c.output_dir;
		}
		for (auto &kv : groups) {
			std::vector<const SrtCue *> &cues = kv.second;
			std::stable_sort(cues.begin(), cues.end(),
					 [](const SrtCue *a, const SrtCue *b) {
						 return a->start_ms < b->start_ms;
					 });
			std::string lbl = sanitize_for_filename(labels[kv.first]);
			if (lbl.empty())
				lbl = "source";
			std::string dir = resolve_output_dir(dirs[kv.first], rec_dir);
			std::string name = base + " " + lbl + suffix + ".srt";
			write_srt_file((fs::path(dir) / name).string(), cues, false);
		}
	};

	// Original (transcription) files.
	write_combined([](const SrtCue &c) { return c.lang_code.empty(); }, "");
	write_per_source([](const SrtCue &c) { return c.lang_code.empty(); }, "");

	// Translated files: one set per target language, BCP 47 code appended.
	std::set<std::string> langs;
	for (const SrtCue &c : g_srt_cues)
		if (!c.lang_code.empty())
			langs.insert(c.lang_code);
	for (const std::string &lang : langs) {
		write_combined([&lang](const SrtCue &c) { return c.lang_code == lang; },
			       " " + lang);
		write_per_source([&lang](const SrtCue &c) { return c.lang_code == lang; },
				 " " + lang);
	}

	g_srt_cues.clear();
}

void srt_session_maybe_end_locked()
{
	if (g_srt_active && !g_srt_recording && !g_srt_streaming) {
		srt_session_finalize_locked();
		g_srt_active = false;
		obs_log(LOG_INFO, "srt_session: ended");
	}
}

} // namespace

bool srt_session_active()
{
	std::lock_guard<std::mutex> lock(g_srt_mutex);
	return g_srt_active;
}

void srt_session_add_cue(const void *source_key, uint64_t start_ms, uint64_t end_ms,
			 const std::string &text, const std::string &label_text,
			 bool label_enabled, bool combined, bool per_source,
			 const std::string &output_dir, const std::string &lang_code)
{
	if (text.empty() || (!combined && !per_source))
		return;

	// Resolve the display label: configured label, else the source host name.
	std::string label = (label_enabled && !label_text.empty()) ? label_text : std::string();
	if (label.empty()) {
		std::lock_guard<std::mutex> lock(g_registry_mutex);
		for (const auto &entry : g_registry) {
			if (entry.gf == source_key) {
				label = entry.host_source_name;
				break;
			}
		}
	}
	if (label.empty())
		label = "Source";

	std::lock_guard<std::mutex> lock(g_srt_mutex);
	if (!g_srt_active)
		return;

	uint64_t rel_start = start_ms > g_srt_start_ms ? start_ms - g_srt_start_ms : 0;
	uint64_t rel_end = end_ms > g_srt_start_ms ? end_ms - g_srt_start_ms : 0;
	if (rel_end <= rel_start)
		rel_end = rel_start + 2000; // ensure a visible duration

	SrtCue cue;
	cue.source = source_key;
	cue.label = label;
	cue.text = text;
	cue.start_ms = rel_start;
	cue.end_ms = rel_end;
	cue.combined = combined;
	cue.per_source = per_source;
	cue.output_dir = output_dir;
	cue.lang_code = lang_code;
	g_srt_cues.push_back(std::move(cue));
}

// ---------------------------------------------------------------------------
// OBS frontend event callback + dock registration
// ---------------------------------------------------------------------------
static QPointer<CaptionContentWidget> g_content;

static void frontend_event_cb(enum obs_frontend_event event, void * /*data*/)
{
	if (event == OBS_FRONTEND_EVENT_SCENE_CHANGED && g_content) {
		g_content->refreshCaptions();
		return;
	}

	switch (event) {
	case OBS_FRONTEND_EVENT_RECORDING_STARTED: {
		std::lock_guard<std::mutex> lock(g_srt_mutex);
		g_srt_recording = true;
		// begin outside isn't needed; inline the start to avoid re-lock
		if (!g_srt_active) {
			g_srt_active = true;
			g_srt_start_ms = now_ms();
			g_srt_cues.clear();
			g_srt_last_rec_path.clear();
			obs_log(LOG_INFO, "srt_session: started (recording)");
		}
		break;
	}
	case OBS_FRONTEND_EVENT_STREAMING_STARTED: {
		std::lock_guard<std::mutex> lock(g_srt_mutex);
		g_srt_streaming = true;
		if (!g_srt_active) {
			g_srt_active = true;
			g_srt_start_ms = now_ms();
			g_srt_cues.clear();
			g_srt_last_rec_path.clear();
			obs_log(LOG_INFO, "srt_session: started (streaming)");
		}
		break;
	}
	case OBS_FRONTEND_EVENT_RECORDING_STOPPED: {
		char *recFile = obs_frontend_get_last_recording();
		std::lock_guard<std::mutex> lock(g_srt_mutex);
		if (recFile && strlen(recFile) > 0)
			g_srt_last_rec_path = recFile;
		if (recFile)
			bfree(recFile);
		g_srt_recording = false;
		srt_session_maybe_end_locked();
		break;
	}
	case OBS_FRONTEND_EVENT_STREAMING_STOPPED: {
		std::lock_guard<std::mutex> lock(g_srt_mutex);
		g_srt_streaming = false;
		srt_session_maybe_end_locked();
		break;
	}
	default:
		break;
	}
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------
void caption_dock_init()
{
	// Create the content widget and register the dock with OBS.
	// obs_frontend_add_dock_by_id is the standard plugin API — it creates
	// the QDockWidget, adds it to the main window, and wires up the Docks
	// menu toggle entry. Call it here (module load) as most plugins do.
	g_content = new CaptionContentWidget(nullptr);
	obs_frontend_add_dock_by_id("BosscatCaptionDock",
				    obs_module_text("caption_dock_title"),
				    g_content);
	obs_log(LOG_INFO, "BOSSCAT caption dock initialized");

	obs_frontend_add_event_callback(frontend_event_cb, nullptr);
}

void caption_dock_shutdown()
{
	obs_frontend_remove_event_callback(frontend_event_cb, nullptr);
	g_content = nullptr;
}
