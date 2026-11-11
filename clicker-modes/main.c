/* SPDX-License-Identifier: GPL-3.0-only */

/* Clicker Modes. Settings > Clicker cycles Off, Speaker, Headphones and Both
 * instead of Off and On. Apple's click engine already routes all four values
 * of its Clicker preference; only the Settings row offers two of them, and its
 * labels for Speaker and Headphones are swapped.
 *
 * Adapted from RepriseOS payload/clicker.c (565dfe0, d48e7c4, c997a51) by
 * alexballvideos-netizen and lgreitis. */

#include "osos-2.0.5.h"
#include "rv_api.h"
#include "rv_libc.h"
#include "rv_sites.h"
#include "rv_ui.h"

static const struct rv_api *api;

static void *method(void *object, uint32_t offset)
{
    return (*(void ***)object)[offset / 4];
}

static uint32_t clicker_get(void *preferences)
{
    typedef uint32_t (*get_fn)(void *, uint32_t, uint32_t);
    return ((get_fn)method(preferences, Preferences_get_method))(preferences, Preference_Clicker,
                                                                 Service_Preferences);
}

static void clicker_set(void *preferences, uint32_t value)
{
    typedef void (*set_fn)(void *, uint32_t, uint32_t, uint32_t);
    ((set_fn)method(preferences, Preferences_set_method))(preferences, Preference_Clicker,
                                                          Service_Preferences, value);
}

static void notify(void *model, uint32_t property)
{
    typedef void (*notify_fn)(void *, uint32_t, uint32_t);
    ((notify_fn)method(model, Model_notify_method))(model, Notify_All, property);
}

static uint32_t on_action(uint32_t controller, uint32_t name, uint32_t argument,
                         const void *next)
{
    void *preferences = PreferencesService_Get();
    if (!preferences || !name || strcmp((const char *)name, "ToggleSetting_Clicker"))
        return api->hook_next(next, controller, name, argument, 0, 0, 0);
    if (argument == ActionSupportQuery)
        return 1;
    uint32_t value;
    switch (clicker_get(preferences)) {
    case Clicker_Off:
        value = Clicker_Speaker;
        break;
    case Clicker_Speaker:
        value = Clicker_Headphones;
        break;
    case Clicker_Headphones:
        value = Clicker_Both;
        break;
    default:
        value = Clicker_Off;
        break;
    }
    clicker_set(preferences, value);
    void *model = *(void **)(controller + ScreenController_settings_model);
    notify(model, SettingsProperty_ClickerValue);
    notify(model, SettingsProperty_ClickerDescription);
    notify(model, SettingsProperty_ClickerImage);
    return 1;
}

static uint32_t on_property(uint32_t model, uint32_t type, uint32_t property, uint32_t out,
                           const void *next)
{
    int image = property == SettingsProperty_ClickerImage && type == Resource_BMap;
    int row = property == SettingsProperty_ClickerValue;
    void *preferences = PreferencesService_Get();
    if (!preferences ||
        (!image && !((row || property == SettingsProperty_ClickerDescription) &&
                     type == Property_String)))
        return api->hook_next(next, model, type, property, out, 0, 0);
    uint32_t value = clicker_get(preferences);
    void *service = UI_GetResourceService();
    if (image) {
        /* Apple's picks the picture by the speaker bit alone. */
        *(uintptr_t *)out = (uintptr_t)ResourceService_GetBitmap(
            service, value == Clicker_Off ? RES_Settings_ClickerOff_Image
                                          : RES_Settings_ClickerOn_Image);
        return 1;
    }
    uint32_t id;
    switch (value) {
    case Clicker_Off:
        id = row ? RES_Settings_Off_String : RES_Settings_ClickerOff_Description;
        break;
    case Clicker_Speaker:
        id = row ? RES_Settings_Speaker_String : RES_Settings_ClickerSpeaker_Description;
        break;
    case Clicker_Headphones:
        id = row ? RES_Settings_Headphones_String : RES_Settings_ClickerHeadphones_Description;
        break;
    default:
        id = row ? RES_Settings_Both_String : RES_Settings_ClickerBoth_Description;
        break;
    }
    *(uintptr_t *)out = (uintptr_t)ResourceService_GetString(service, id);
    return 1;
}

static const struct rv_hook hooks[] = {
    RV_HOOK_FILTER(RV_SITE_Settings_Action_slot, on_action),
    RV_HOOK_FILTER(RV_SITE_Settings_Property_slot, on_property),
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
