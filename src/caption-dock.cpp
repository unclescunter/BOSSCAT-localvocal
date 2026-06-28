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
#include <QScrollArea>
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
	// Remove stale entry for same gf pointer (shouldn't happen, but guard).
	g_registry.erase(std::remove_if(g_registry.begin(), g_registry.end(),
					[gf](const CaptionFilterEntry &e) { return e.gf == gf; }),
			 g_registry.end());
	g_registry.push_back({gf, host_source_name});
}

void caption_registry_remove(transcription_filter_data *gf)
{
	std::lock_guard<std::mutex> lock(g_registry_mutex);
	g_registry.erase(std::remove_if(g_registry.begin(), g_registry.end(),
					[gf](const CaptionFilterEntry &e) { return e.gf == gf; }),
			 g_registry.end());
}

// ---------------------------------------------------------------------------
// Build a snapshot of (label, caption) for sources in the current scene.
// Must be called on the OBS/UI thread (no OBS thread locks here).
// ---------------------------------------------------------------------------
static std::vector<std::pair<std::string, std::string>> get_scene_captions()
{
	std::vector<std::pair<std::string, std::string>> out;

	obs_source_t *scene_src = obs_frontend_get_current_scene();
	if (!scene_src)
		return out;

	// Collect source names in the current scene.
	std::vector<std::string> scene_source_names;
	obs_scene_t *scene = obs_scene_from_source(scene_src);
	if (scene) {
		obs_scene_enum_items(
			scene,
			[](obs_scene_t *, obs_sceneitem_t *item, void *userdata) {
				auto *names =
					static_cast<std::vector<std::string> *>(userdata);
				obs_source_t *src = obs_sceneitem_get_source(item);
				if (src)
					names->push_back(obs_source_get_name(src));
				return true;
			},
			&scene_source_names);
	}
	obs_source_release(scene_src);

	// Build caption snapshot (copy under lock, then release before Qt call).
	std::vector<CaptionFilterEntry> snap;
	{
		std::lock_guard<std::mutex> lock(g_registry_mutex);
		snap = g_registry;
	}

	for (const auto &entry : snap) {
		bool in_scene = std::find(scene_source_names.begin(), scene_source_names.end(),
					  entry.host_source_name) != scene_source_names.end();
		if (!in_scene)
			continue;

		std::string caption = entry.gf->captions_monitor.getLastEmitted();
		std::string label = entry.gf->caption_label_enabled
					    ? entry.gf->caption_label_text
					    : entry.host_source_name;
		out.push_back({label, caption});
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
		captionLabel->setText("(no captions — no active sources in scene)");

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
		auto rows = get_scene_captions();
		if (rows.empty()) {
			captionLabel->setText("(no active captions in current scene)");
			return;
		}
		QString text;
		for (const auto &row : rows) {
			if (!text.isEmpty())
				text += "\n\n";
			if (!row.first.empty())
				text += QString::fromStdString(row.first) + ": ";
			text += QString::fromStdString(row.second);
		}
		captionLabel->setText(text.isEmpty() ? "(silence)" : text);
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
	// Must be called from the UI thread (obs_module_load runs on main thread).
	auto *main_window = static_cast<QMainWindow *>(obs_frontend_get_main_window());
	if (!main_window) {
		obs_log(LOG_WARNING, "caption_dock_init: no main window, dock skipped");
		return;
	}

	g_dock = new CaptionDockWidget(main_window);

	// obs_frontend_add_dock_by_id takes ownership.
	if (!obs_frontend_add_dock_by_id("BosscatCaptionDock",
					 obs_module_text("caption_dock_title"), g_dock)) {
		obs_log(LOG_WARNING, "caption_dock_init: obs_frontend_add_dock_by_id failed");
		// g_dock is owned by the parent window at this point.
		g_dock = nullptr;
		return;
	}

	obs_frontend_add_event_callback(frontend_event_cb, nullptr);
	obs_log(LOG_INFO, "BOSSCAT caption dock initialized");
}

void caption_dock_shutdown()
{
	obs_frontend_remove_event_callback(frontend_event_cb, nullptr);
	// The dock widget is managed by OBS's dock system; don't delete it manually.
	g_dock = nullptr;
}
