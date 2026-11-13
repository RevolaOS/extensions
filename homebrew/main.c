/* SPDX-License-Identifier: GPL-3.0-only */

/* Packages under /Revola/Games/ and /Revola/Apps/ in Apple's Games list. The
 * games service builds its one list once, so the build entry clears it whenever
 * what it holds is not what is wanted (games, or apps while the Apps screen is
 * open) and Apple scans again before a screen draws it. The manifest reader is
 * handed no signature path for a package, which is how Apple itself reads a
 * manifest it does not verify.
 *
 * Adapted from RepriseOS payload/homebrew.c (fd5d367) by lgreitis. */

#include <stddef.h>

#include "osos-2.0.5.h"
#include "rv_api.h"
#include "rv_libc.h"
#include "rv_menus.h"
#include "rv_sites.h"
#include "rv_ui.h"

#define PATH_BYTES 258 /* the text of Apple's legacy path, as the core's file layer */

static const struct rv_api *api;
static rv_extension_handle self;

/* A root as the games service scans it and as the manifest's content folder. */
struct root {
    const char *path;
    const char *content;
};
static const struct root games_root = {"/Revola/Games/", "Games"};
static const struct root apps_root = {"/Revola/Apps/", "Apps"};

/* The root the last scan listed and its ids, NUL separated, ending in an empty
 * name; what GameFiles_BuildPath redirects. */
static const struct root *listed_root;
static char *listed_ids;

/* What the list is wanted as, and what it holds: games, or apps while the Apps
 * screen is open. A list built before the extension loaded is dropped at the
 * first build after, so it is scanned again with the hooks in place. */
enum listing { GAMES, APPS };
static enum listing wanted, built;
static int fresh = 1;
/* The games service, from its first list build through the hooks; NULL until then. */
static void *service;

/* 1 for the `n` bytes of a folder name that is one package id: not empty, not
 * starting with a dot, no path or drive separator. */
static int is_package_id(const char *name, unsigned n)
{
    if (!n || name[0] == '.')
        return 0;
    for (unsigned i = 0; i < n; i++)
        if (name[i] == '/' || name[i] == '\\' || name[i] == ':')
            return 0;
    return 1;
}

/* 1 for root + id + "/Manifest.plist" under one of the roots. */
static int is_homebrew_manifest(const char *path)
{
    const struct root *const roots[] = {&games_root, &apps_root};

    if (!path)
        return 0;
    for (unsigned r = 0; r < sizeof roots / sizeof roots[0]; r++) {
        unsigned n = strlen(roots[r]->path);
        if (memcmp(path, roots[r]->path, n) != 0)
            continue;
        const char *id = path + n, *end = id;
        while (*end && *end != '/')
            end++;
        return is_package_id(id, end - id) && strcmp(end, "/Manifest.plist") == 0;
    }
    return 0;
}

static uint32_t on_manifest_reader(void *reader, const char *manifest, uint32_t volume,
                                   const char *signature, const void *next)
{
    if (volume == FileVolume_Disk && is_homebrew_manifest(manifest))
        signature = 0;
    return api->hook_next(next, (uint32_t)reader, (uint32_t)manifest, volume,
                          (uint32_t)signature, 0, 0);
}

struct folders {
    uint32_t bytes;
    char *out;
};

static void count_folder(const char *name, int is_folder, void *arg)
{
    struct folders *f = arg;
    if (is_folder && is_package_id(name, strlen(name)))
        f->bytes += strlen(name) + 1;
}

static void copy_folder(const char *name, int is_folder, void *arg)
{
    struct folders *f = arg;
    if (!is_folder || !is_package_id(name, strlen(name)))
        return;
    unsigned n = strlen(name) + 1;
    memcpy(f->out, name, n);
    f->out += n;
}

/* Remembers the ids under `root`, then lets Apple scan it. */
static void scan_root(void *games, void *list, const struct root *root)
{
    struct folders f = {1, NULL};
    api->content_list(self, root->content, count_folder, &f);
    char *ids = api->alloc(self, f.bytes);
    if (!ids)
        return;
    f.out = ids;
    api->content_list(self, root->content, copy_folder, &f);
    *f.out = 0;
    if (listed_ids)
        api->free(self, listed_ids);
    listed_ids = ids;
    listed_root = root;
    GamesService_ScanRoot(games, list, FileVolume_Disk, root->path);
}

static uint32_t on_build_list(void *games, const void *next)
{
    service = games;
    if (fresh || built != wanted) {
        GamesService_ClearList(games);
        built = wanted;
        fresh = 0;
    }
    return api->hook_next(next, (uint32_t)games, 0, 0, 0, 0, 0);
}

static uint32_t on_scan_built_in_root(void *games, void *list, uint32_t volume,
                                      const char *root, const void *next)
{
    if (wanted == APPS)
        return 0;
    return api->hook_next(next, (uint32_t)games, (uint32_t)list, volume, (uint32_t)root, 0, 0);
}

static uint32_t on_scan_disk_root(void *games, void *list, uint32_t volume, const char *root,
                                  const void *next)
{
    if (wanted == APPS) {
        scan_root(games, list, &apps_root);
        return 0;
    }
    uint32_t result =
        api->hook_next(next, (uint32_t)games, (uint32_t)list, volume, (uint32_t)root, 0, 0);
    scan_root(games, list, &games_root);
    return result;
}

static int listed(const char *id)
{
    if (!listed_ids)
        return 0;
    for (const char *name = listed_ids; *name; name += strlen(name) + 1)
        if (strcmp(name, id) == 0)
            return 1;
    return 0;
}

/* A listed id's path is under its RevolaOS root; one that does not fit stays
 * Apple's. */
static uint32_t on_build_path(struct NativeString *out, uint32_t kind, struct NativeString *id,
                              uint32_t storage, const void *next, const char *leaf)
{
    const char *name = id ? NativeString_GetUtf8(id) : NULL;
    char path[PATH_BYTES];
    if (leaf && *leaf == '/')
        leaf++;
    if (kind == GameDirectory_Package && storage == GameStorage_Disk && name && listed(name)) {
        unsigned root_n = strlen(listed_root->path), name_n = strlen(name);
        unsigned leaf_n = leaf ? strlen(leaf) : 0;
        if (root_n + name_n + 1 + leaf_n < sizeof path) {
            memcpy(path, listed_root->path, root_n);
            memcpy(path + root_n, name, name_n);
            path[root_n + name_n] = '/';
            memcpy(path + root_n + name_n + 1, leaf, leaf_n);
            path[root_n + name_n + 1 + leaf_n] = 0;
            NativeString_InitUtf8(out, path);
            return 0;
        }
    }
    return api->hook_next(next, (uint32_t)out, kind, (uint32_t)id, storage, (uint32_t)leaf, 0);
}

/* The caption of a row while the list is apps: Apple's words for the play count
 * with Opened in place of Played, the last still formatted by Apple. */
static uint32_t on_played_string(void *resources, uint32_t id, const void *next)
{
    if (wanted == APPS) {
        if (id == RES_GamesMenu_NeverPlayed_String)
            return (uint32_t)"Never Opened";
        if (id == RES_GamesMenu_PlayedOnce_String)
            return (uint32_t)"Opened Once";
        if (id == RES_GamesMenu_PlayedTimes_String)
            return (uint32_t)"Opened %d Times";
    }
    return api->hook_next(next, (uint32_t)resources, id, 0, 0, 0, 0);
}

/* The Apps row: the list is wanted as apps from before the push, which builds
 * it, until the screen closes; the next build after that is games again. */
static void open_apps(const struct rv_context *ctx)
{
    wanted = APPS;
    if (api->ui_push(ctx->self, "apps", NULL) != RV_OK)
        wanted = GAMES;
}

static void apps_closed(const struct rv_context *ctx)
{
    (void)ctx;
    wanted = GAMES;
}

static void count_package(const char *name, int is_folder, void *arg)
{
    if (is_folder && is_package_id(name, strlen(name)))
        ++*(unsigned *)arg;
}

/* The Extras pane beside the Apps row: how many packages the folder holds. */
static const char *apps_caption(const struct rv_context *ctx)
{
    static char caption[16];
    unsigned count = 0;
    (void)ctx;
    api->content_list(self, apps_root.content, count_package, &count);
    rv_format_int(caption, sizeof caption, (int)count, count == 1 ? " App" : " Apps");
    return caption;
}

/* Apple builds the list once, so what it holds follows the hooks that were bound
 * when it was built: dropped here so Apple builds it again on demand without
 * them. A list never built through the hooks holds nothing of ours. */
static void unload(void)
{
    if (service)
        GamesService_ClearList(service);
    if (listed_ids)
        api->free(self, listed_ids);
    listed_ids = NULL;
}

static const struct rv_hook hooks[] = {
    RV_HOOK_FILTER(RV_SITE_GameManifest_InitReader_call, on_manifest_reader),
    RV_HOOK_FILTER(RV_SITE_GamesService_BuildList_entry, on_build_list),
    RV_HOOK_FILTER(RV_SITE_GamesService_ScanBuiltInRoot_call, on_scan_built_in_root),
    RV_HOOK_FILTER(RV_SITE_GamesService_ScanDiskRoot_call, on_scan_disk_root),
    RV_HOOK_FILTER(RV_SITE_GameFiles_BuildPath_entry, on_build_path),
    RV_HOOK_FILTER(RV_SITE_GamesService_PlayedString_call, on_played_string),
    RV_HOOK_FILTER(RV_SITE_GamesService_PlayedTimesString_call, on_played_string),
};

static const struct rv_screen screens[] = {
    RV_SCREEN_STOCK("apps", "Apps", RV_STOCK_SCREEN_GAMES, .closed = apps_closed),
};

static const struct rv_menu_entry entries[] = {
    RV_MENU_ENTRY(RV_MENU_EXTRAS, RV_EXTRAS_GAMES, RV_ROW_ACTION("Apps", open_apps),
                  .caption_text = apps_caption, .bitmap = RV_BITMAP("apps")),
};

static const struct rv_descriptor extension = {
    .size = sizeof extension,
    RV_HOOKS(hooks),
    RV_SCREENS(screens),
    RV_ENTRIES(entries),
    .unload = unload,
};

const struct rv_descriptor *rv_module_entry(const struct rv_api *core, rv_extension_handle handle)
{
    api = core;
    self = handle;
    return &extension;
}
