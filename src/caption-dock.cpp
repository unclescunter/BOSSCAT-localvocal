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
#include <QVBoxLayout>
#include <QWidget>
#include <QTimer>
#include <QString>
#include <QMainWindow>

#include <mutex>
#include <vector>
#include <string>

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

// Called from output_text() to push the latest caption into the registry.
// Safe to call from any thread.
void caption_dock_update(transcription_filter_data *gf, const std::string &caption,
			 const std::string &label, bool label_enabled)
{
	std::lock_guard<std::mutex> lock(g_registry_mutex);
	for (auto &entry : g_registry) {
		if (entry.gf == gf) {
			entry.last_caption = caption;
			entry.label = label;
			entry.label_enabled = label_enabled;
			break;
		}
	}
}

// ---------------------------------------------------------------------------
// Build a snapshot for the dock.
// All caption text is copied under the registry lock, so gf is never
// dereferenced after the lock is released (no use-after-free).
// All registered filters are shown — global audio sources (Mic/Aux in OBS's
// audio mixer) never appear as scene items and would be silently dropped by
// scene-scoped enumeration.
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

// ---------------------------------------------------------------------------
// The dock widget
// ---------------------------------------------------------------------------
class CaptionDockWidget : public QDockWidget {
public:
	explicit CaptionDockWidget(QWidget *parent = nullptr)
		: QDockWidget(obs_module_text("caption_dock_title"), parent)
	{
		setObjectName("BosscatCaptionDock");
		setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable);

		auto *container = new QWidget(this);
		auto *layout = new QVBoxLayout(container);
		layout->setContentsMargins(4, 4, 4, 4);
		layout->setSpacing(4);

		captionLabel = new QLabel(container);
		captionLabel->setWordWrap(true);
		captionLabel->setAlignment(Qt::AlignTop | Qt::AlignLeft);
		captionLabel->setTextFormat(Qt::PlainText);
		captionLabel->setStyleSheet("QLabel { font-size: 14pt; }");
		captionLabel->setText("(no localvocal filters active)");

		layout->addWidget(captionLabel);
		layout->addStretch();
		container->setLayout(layout);
		setWidget(container);

		refreshTimer = new QTimer(this);
		connect(refreshTimer, &QTimer::timeout, this, &CaptionDockWidget::refreshCaptions);
		refreshTimer->start(200);
	}

	void refreshCaptions()
	{
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
			text += row.second.empty() ? "(listening...)"
						   : QString::fromStdString(row.second);
		}
		captionLabel->setText(text);
	}

private:
	QLabel *captionLabel = nullptr;
	QTimer *refreshTimer = nullptr;
};

// ---------------------------------------------------------------------------
// OBS frontend event callback
// ---------------------------------------------------------------------------
static CaptionDockWidget *g_dock = nullptr;

static void frontend_event_cb(enum obs_frontend_event event, void * /*data*/)
{
	if (event == OBS_FRONTEND_EVENT_SCENE_CHANGED && g_dock)
		g_dock->refreshCaptions();
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------
void caption_dock_init()
{
	auto *main_window = static_cast<QMainWindow *>(obs_frontend_get_main_window());
	if (!main_window) {
		obs_log(LOG_WARNING, "caption_dock_init: no main window, dock skipped");
		return;
	}

	g_dock = new CaptionDockWidget(main_window);

	if (!obs_frontend_add_dock_by_id("BosscatCaptionDock",
					 obs_module_text("caption_dock_title"), g_dock)) {
		obs_log(LOG_WARNING, "caption_dock_init: obs_frontend_add_dock_by_id failed");
		g_dock = nullptr;
		return;
	}

	obs_frontend_add_event_callback(frontend_event_cb, nullptr);
	obs_log(LOG_INFO, "BOSSCAT caption dock initialized");
}

void caption_dock_shutdown()
{
	obs_frontend_remove_event_callback(frontend_event_cb, nullptr);
	g_dock = nullptr;
}
