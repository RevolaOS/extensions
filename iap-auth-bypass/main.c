/* SPDX-License-Identifier: GPL-3.0-only */

/* Both accessory signature verifiers return zero on success. The General Lingo
 * 0x18 handler then acknowledges success and marks the accessory authenticated.
 *
 * Adapted from RepriseOS payload/iap_auth.c (c99dc33) by RaduTek. */

#include "rv_api.h"
#include "rv_sites.h"
#include "rv_ui.h"

static uint32_t verify_rsa(void *manager, uint32_t channel, uint32_t key_id,
                           void *signature, const void *next, uint32_t signature_bytes)
{
    (void)manager;
    (void)channel;
    (void)key_id;
    (void)signature;
    (void)next;
    (void)signature_bytes;
    return 0;
}

static uint32_t verify_alternate(void *authentication, void *signature,
                                 uint32_t signature_bytes, const void *next)
{
    (void)authentication;
    (void)signature;
    (void)signature_bytes;
    (void)next;
    return 0;
}

static const struct rv_hook hooks[] = {
    RV_HOOK_REPLACE(RV_SITE_IAP_VerifyDeviceSignatureRSA_call, verify_rsa),
    RV_HOOK_REPLACE(RV_SITE_IAP_VerifyDeviceSignatureAlternate_call, verify_alternate),
};

static const struct rv_descriptor extension = {
    .size = sizeof extension,
    RV_HOOKS(hooks),
};

const struct rv_descriptor *rv_module_entry(const struct rv_api *core, rv_extension_handle handle)
{
    (void)core;
    (void)handle;
    return &extension;
}
