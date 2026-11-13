/* SPDX-License-Identifier: GPL-3.0-only */

/* Packages under the RevolaOS roots load without Apple's signature: when the
 * loader reads a manifest from one of them, the signature path it passes is
 * dropped, which is how Apple itself reads a manifest it does not verify.
 *
 * Adapted from RepriseOS payload/homebrew.c (fd5d367) by lgreitis. */

#include "rv_api.h"
#include "rv_sites.h"
#include "rv_ui.h"

static const struct rv_api *api;

static const char *const roots[] = {"/Revola/Games/", "/Revola/Apps/"};

/* 1 for root + id + "/Manifest.plist" with an id that names one folder. */
static int is_homebrew_manifest(const char *path)
{
    static const char filename[] = "/Manifest.plist";

    if (!path)
        return 0;
    for (unsigned r = 0; r < sizeof roots / sizeof roots[0]; r++) {
        const char *root = roots[r];
        const char *p = path;
        unsigned i = 0;
        for (; root[i] && p[i] == root[i]; i++)
            ;
        if (root[i])
            continue;
        p += i;
        if (!*p || *p == '/' || *p == '.')
            return 0;
        while (*p && *p != '/') {
            if (*p == '\\' || *p == ':')
                return 0;
            p++;
        }
        for (i = 0; filename[i] && p[i] == filename[i]; i++)
            ;
        return filename[i] == 0 && p[i] == 0;
    }
    return 0;
}

static uint32_t on_manifest_reader(void *reader, const char *manifest, uint32_t mode,
                                   const char *signature, const void *next)
{
    if (mode == 0 && is_homebrew_manifest(manifest))
        signature = 0;
    return api->hook_next(next, (uint32_t)reader, (uint32_t)manifest, mode, (uint32_t)signature,
                          0, 0);
}

static const struct rv_hook hooks[] = {
    RV_HOOK_FILTER(RV_SITE_GameManifest_InitReader_call, on_manifest_reader),
};

static const struct rv_descriptor extension = {
    .size = sizeof extension,
    RV_HOOKS(hooks),
};

const struct rv_descriptor *rv_module_entry(const struct rv_api *core, rv_extension_handle handle)
{
    (void)handle;
    api = core;
    return &extension;
}
