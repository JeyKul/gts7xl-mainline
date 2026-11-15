/*
 * Downloads for the framebuffer frontend: saved whole to a folder, with
 * progress and the result in the window's status bar. Into
 * $NETSURF_DOWNLOAD_DIR (or /tmp/Download), unless the file wouldn't fit.
 *
 * NetSurf is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 */
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statvfs.h>

#include "utils/errors.h"
#include "netsurf/download.h"
#include "desktop/download.h"
#include "netsurf/window.h"

#include "framebuffer/download.h"

void gui_window_set_status(struct gui_window *g, const char *text);

struct gui_download_window {
	struct gui_window *parent;
	FILE *f;
	char path[1024];
	unsigned long long total, have, shown;
};

static const char *pick_dir(unsigned long long need)
{
	const char *dir = getenv("NETSURF_DOWNLOAD_DIR");
	struct statvfs s;

	if (dir && *dir) {
		mkdir(dir, 0755);
		if (!statvfs(dir, &s) && (need == 0 || (unsigned long long)s.f_bavail * s.f_frsize > need + (64 << 20)))
			return dir;
	}
	mkdir("/tmp/Download", 0755);
	return "/tmp/Download";
}

static void status(struct gui_download_window *dw, const char *what)
{
	char t[1200];
	const char *name = strrchr(dw->path, '/');

	snprintf(t, sizeof(t), "%s %s", what, name ? name + 1 : dw->path);
	gui_window_set_status(dw->parent, t);
}

static struct gui_download_window *fb_download_create(struct download_context *ctx, struct gui_window *parent)
{
	struct gui_download_window *dw = calloc(1, sizeof(*dw));
	const char *name = download_context_get_filename(ctx);

	if (!dw)
		return NULL;
	dw->parent = parent;
	dw->total = download_context_get_total_length(ctx);
	if (!name || !*name || strchr(name, '/'))
		name = "download";
	snprintf(dw->path, sizeof(dw->path), "%s/%s", pick_dir(dw->total), name);
	dw->f = fopen(dw->path, "wb");
	if (!dw->f) {
		status(dw, "Cannot save");
		free(dw);
		return NULL;
	}
	status(dw, "Downloading");
	return dw;
}

static nserror fb_download_data(struct gui_download_window *dw, const char *data, unsigned int size)
{
	char t[1200];

	if (fwrite(data, 1, size, dw->f) != size)
		return NSERROR_SAVE_FAILED;
	dw->have += size;
	/* a status update every 4 MB */
	if (dw->have - dw->shown >= (4 << 20)) {
		dw->shown = dw->have;
		if (dw->total)
			snprintf(t, sizeof(t), "Downloading %s: %llu of %llu MB", strrchr(dw->path, '/') + 1,
				 dw->have >> 20, dw->total >> 20);
		else
			snprintf(t, sizeof(t), "Downloading %s: %llu MB", strrchr(dw->path, '/') + 1, dw->have >> 20);
		gui_window_set_status(dw->parent, t);
	}
	return NSERROR_OK;
}

static void fb_download_error(struct gui_download_window *dw, const char *error_msg)
{
	(void)error_msg;
	fclose(dw->f);
	dw->f = NULL;
	remove(dw->path);
	status(dw, "Download failed:");
}

static void fb_download_done(struct gui_download_window *dw)
{
	if (dw->f) {
		fclose(dw->f);
		status(dw, "Saved");
	}
	free(dw);
}

static struct gui_download_table download_table = {
	.create = fb_download_create,
	.data = fb_download_data,
	.error = fb_download_error,
	.done = fb_download_done,
};

struct gui_download_table *framebuffer_download_table = &download_table;
