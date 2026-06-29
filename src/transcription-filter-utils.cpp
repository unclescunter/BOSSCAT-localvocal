#include "transcription-filter-utils.h"

#include <obs-module.h>
#include <obs.h>
#include <obs-frontend-api.h>
#include <plugin-support.h>

bool add_sources_to_list(void *list_property, obs_source_t *source)
{
	const char *source_id = obs_source_get_id(source);
	if (strcmp(source_id, "text_ft2_source_v2") != 0 &&
	    strcmp(source_id, "text_gdiplus_v3") != 0 &&
	    strcmp(source_id, "text_gdiplus_v2") != 0) {
		return true;
	}

	obs_property_t *sources = (obs_property_t *)list_property;
	const char *name = obs_source_get_name(source);
	obs_property_list_add_string(sources, name, name);
	return true;
}
