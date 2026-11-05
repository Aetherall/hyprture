/* hyprture's C API, for other Hyprland plugins: apertures on a monitor.
 * (Clients use the protocol, protocol/hyprture-v1.xml, instead.)
 *
 * hyprture exports these symbols from its shared object. Find it among the
 * loaded plugins by name ("hyprture"), dlsym() them from its handle, and check
 * hyprture_api_version() against HYPRTURE_API_VERSION before calling anything
 * else. hyprture may be loaded after you, or unloaded and reloaded: resolve
 * the symbols again whenever its handle changes. Its apertures go with it.
 * Copy this header into your plugin; the version check guards against it
 * going stale.
 *
 * Call from Hyprland's main thread only.
 */
#ifndef HYPRTURE_H
#define HYPRTURE_H

#include <stdbool.h>
#include <stdint.h>

#define HYPRTURE_PLUGIN_NAME "hyprture"
#define HYPRTURE_API_VERSION 1

#ifdef __cplusplus
extern "C" {
#endif

enum hyprture_source_kind {
    HYPRTURE_SOURCE_NONE      = 0,
    HYPRTURE_SOURCE_WORKSPACE = 1, /* `workspace`, with `scene` */
    HYPRTURE_SOURCE_WINDOW    = 2, /* `window`: its address, "0x..." */
    HYPRTURE_SOURCE_MONITOR   = 3, /* `monitor`: its active workspace, with the shell */
};

enum hyprture_scene {
    HYPRTURE_SCENE_WINDOWS = 0, /* the workspace's windows */
    HYPRTURE_SCENE_SHELL   = 1, /* and the wallpaper and layer surfaces */
};

enum hyprture_level {
    HYPRTURE_LEVEL_OVERLAY = 0, /* above everything */
    HYPRTURE_LEVEL_WINDOWS = 1, /* above windows, below top and overlay layers */
};

typedef struct {
    int         kind;
    int64_t     workspace;
    const char* window;
    const char* monitor;
    int         scene;
} hyprture_source;

/* The API version this hyprture implements. */
int hyprture_api_version(void);

/* A new aperture on the monitor named `monitor`, showing nothing yet. Its id,
 * never 0. */
uint64_t hyprture_create(const char* monitor, int level);

/* What it shows. Copied: the strings can go after the call. */
bool hyprture_set_source(uint64_t id, const hyprture_source* source);

/* Where: a monitor-local logical box the source is fitted into, centered. */
bool hyprture_set_box(uint64_t id, double x, double y, double w, double h);

/* How: opacity (0-1), corner radius (logical), live (else redrawn only when
 * its state changes). */
bool hyprture_set_style(uint64_t id, float opacity, float radius, bool live);

/* The source's logical size; false while it doesn't exist. */
bool hyprture_source_size(uint64_t id, double* w, double* h);

void hyprture_destroy(uint64_t id);

typedef int (*hyprture_api_version_fn)(void);
typedef uint64_t (*hyprture_create_fn)(const char*, int);
typedef bool (*hyprture_set_source_fn)(uint64_t, const hyprture_source*);
typedef bool (*hyprture_set_box_fn)(uint64_t, double, double, double, double);
typedef bool (*hyprture_set_style_fn)(uint64_t, float, float, bool);
typedef bool (*hyprture_source_size_fn)(uint64_t, double*, double*);
typedef void (*hyprture_destroy_fn)(uint64_t);

#ifdef __cplusplus
}
#endif

#endif
