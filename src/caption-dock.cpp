// This file is part of BOSSCAT-localvocal, a GPL-2.0 fork of obs-localvocal
// by Roy Shilkrot (https://github.com/locaal-ai/obs-localvocal).
// Modifications (c) 2025 unclescunter, licensed GPL-2.0. See CHANGES.md.

#include "caption-dock.h"
#include "transcription-filter-data.h"
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
			entry.text_source_name = gf->text_source_name;
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
	std::vector<std::string> text_source_names;
	{
		std::lock_guard<std::mutex> lock(g_registry_mutex);
		for (const auto &entry : g_registry)
			if (!entry.text_source_name.empty())
				text_source_names.push_back(entry.text_source_name);
	}
	for (const auto &name : text_source_names) {
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
		captionLabel->setText("(no localvocal filters active)");
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
			captionLabel->setText("(no localvocal filters active)");
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
// OBS frontend event callback
// Dock creation is deferred to OBS_FRONTEND_EVENT_FINISHED_LOADING.
// obs_module_load fires before the main window is fully shown — calling
// addDockWidget at that point produces a dock that never appears.
// ---------------------------------------------------------------------------
static QPointer<CaptionContentWidget> g_content;

static void frontend_event_cb(enum obs_frontend_event event, void * /*data*/)
{
	if (event == OBS_FRONTEND_EVENT_FINISHED_LOADING) {
		auto *main_window =
			static_cast<QMainWindow *>(obs_frontend_get_main_window());
		if (!main_window) {
			obs_log(LOG_WARNING,
				"caption_dock: no main window at FINISHED_LOADING");
			return;
		}

		g_content = new CaptionContentWidget(nullptr);

		auto *dock = new QDockWidget(
			obs_module_text("caption_dock_title"), main_window);
		dock->setObjectName("BosscatCaptionDock");
		dock->setWidget(g_content);
		dock->setAllowedAreas(Qt::AllDockWidgetAreas);
		main_window->addDockWidget(Qt::RightDockWidgetArea, dock);
		dock->show();

		obs_log(LOG_INFO, "BOSSCAT caption dock initialized");
	}

	if (event == OBS_FRONTEND_EVENT_SCENE_CHANGED && g_content)
		g_content->refreshCaptions();
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------
void caption_dock_init()
{
	obs_frontend_add_event_callback(frontend_event_cb, nullptr);
}

void caption_dock_shutdown()
{
	obs_frontend_remove_event_callback(frontend_event_cb, nullptr);
	g_content = nullptr;
}
