/* SPDX-License-Identifier: GPL-3.0-only */

#include "theme.h"
#include "crc.h"
#include "ext.h"
#include "pixel.h"
#include "rv_libc.h"
#include <stddef.h>

#define FOLDER "Themes"
#define SUFFIX ".rvt"
/* The id of the built-in theme, which is the asset's name; a file of that
 * name in the folder is ignored. */
#define RESERVED_ID "classic"

static int id_valid(const char *id)
{
    uint32_t n = 0;
    for (; n < RVT_ID_BYTES && id[n]; n++) {
        char c = id[n];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || (n && (c == '_' || c == '-'))))
            return 0;
    }
    if (!n || n > RVT_ID_BYTES - 1)
        return 0;
    for (; n < RVT_ID_BYTES; n++)
        if (id[n])
            return 0;
    return !!memcmp(id, RESERVED_ID, sizeof RESERVED_ID);
}

/* Printable UTF-8 up to the first NUL, zeros after it. */
static int name_valid(const uint8_t *text, uint32_t size)
{
    uint32_t at = 0;
    if (!text[0])
        return 0;
    while (at < size && text[at]) {
        uint32_t cp = text[at++], tails = 0, minimum = 0;
        if (cp >= 0xc2 && cp <= 0xdf)
            cp &= 31, tails = 1, minimum = 0x80;
        else if (cp >= 0xe0 && cp <= 0xef)
            cp &= 15, tails = 2, minimum = 0x800;
        else if (cp >= 0xf0 && cp <= 0xf4)
            cp &= 7, tails = 3, minimum = 0x10000;
        else if (cp >= 0x80)
            return 0;
        if (tails > size - at)
            return 0;
        while (tails--) {
            uint32_t c = text[at++];
            if ((c & 0xc0) != 0x80)
                return 0;
            cp = cp << 6 | (c & 63);
        }
        if (cp < minimum || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff) || cp < 32 ||
            cp == 127)
            return 0;
    }
    if (at == size)
        return 0;
    for (; at < size; at++)
        if (text[at])
            return 0;
    return 1;
}

/* The header (RVT_HEADER_BYTES) with its fields checked: magic, version,
 * header size, checksum, fingerprint, appearances and name. The id is the
 * caller's to compare. */
static int header_read(struct rv_file *file, uint8_t *header)
{
    uint32_t got = 0;
    if (api->file_seek(self, file, 0) != RV_OK ||
        api->file_read(self, file, header, RVT_HEADER_BYTES, &got) != RV_OK ||
        got != RVT_HEADER_BYTES)
        return 0;
    uint8_t zero[4] = {0};
    uint32_t computed = crc32_update(0xffffffffu, header, RVT_HEADER_CRC);
    computed = crc32_update(computed, zero, 4);
    computed = ~crc32_update(computed, header + RVT_HEADER_ID, RVT_HEADER_BYTES - RVT_HEADER_ID);
    uint32_t mask = le32(header + RVT_HEADER_APPEARANCES);
    uint32_t header_bytes = le16(header + RVT_HEADER_HEADER_BYTES);
    return le32(header + RVT_HEADER_MAGIC) == RVT_MAGIC &&
           le16(header + RVT_HEADER_VERSION) == RVT_VERSION && header_bytes >= RVT_HEADER_BYTES &&
           header_bytes % 4 == 0 && computed == le32(header + RVT_HEADER_CRC) &&
           !memcmp(header + RVT_HEADER_FINGERPRINT, catalog_fingerprint(), RVT_FINGERPRINT_BYTES) &&
           mask && !(mask & ~(uint32_t)(RVT_APPEARANCE_LIGHT | RVT_APPEARANCE_DARK)) &&
           name_valid(header + RVT_HEADER_NAME, RVT_NAME_BYTES);
}

/* The light or dark pack's place from a checked header: 0 when the header
 * has no such pack, puts it before its own end or off a word, or sizes it
 * below a pack header and style prefix. */
static int pack_place(const uint8_t *header, int dark, struct pack_place *pack)
{
    uint32_t at = dark ? RVT_HEADER_DARK_OFFSET : RVT_HEADER_LIGHT_OFFSET;
    uint32_t offset = le32(header + at), bytes = le32(header + at + 4);
    if (!offset || bytes < RVT_PACK_HEADER_BYTES + RVT_STYLE_BYTES || offset % 4 ||
        offset < le16(header + RVT_HEADER_HEADER_BYTES) || bytes > 0xffffffffu - offset)
        return 0;
    pack->offset = offset;
    pack->bytes = bytes;
    return 1;
}

/* A checked header of theme `id` into `look`, or 0 for another id. */
static int header_look(const uint8_t *header, const char *id, struct look *look)
{
    if (memcmp(header + RVT_HEADER_ID, id, RVT_ID_BYTES))
        return 0;
    memset(look, 0, sizeof *look);
    memcpy(look->id, header + RVT_HEADER_ID, RVT_ID_BYTES);
    memcpy(look->name, header + RVT_HEADER_NAME, RVT_NAME_BYTES);
    look->appearances = le32(header + RVT_HEADER_APPEARANCES);
    look->header_crc = le32(header + RVT_HEADER_CRC);
    return 1;
}

static char lower(char c)
{
    return c >= 'A' && c <= 'Z' ? (char)(c + ('a' - 'A')) : c;
}

/* The id a file name stands for: its stem in lower case, since FAT may return
 * short names in capitals; 0 for another suffix or a stem that is no id. */
static int stem_id(const char *entry, char id[RVT_ID_BYTES])
{
    uint32_t n = 0;
    while (entry[n])
        n++;
    if (n < sizeof SUFFIX || n - (sizeof SUFFIX - 1) > RVT_ID_BYTES - 1)
        return 0;
    for (uint32_t i = 0; i < sizeof SUFFIX - 1; i++)
        if (lower(entry[n - (sizeof SUFFIX - 1) + i]) != SUFFIX[i])
            return 0;
    n -= sizeof SUFFIX - 1;
    memset(id, 0, RVT_ID_BYTES);
    for (uint32_t i = 0; i < n; i++)
        id[i] = lower(entry[i]);
    return id_valid(id);
}

static void path_of(const char *id, char *path)
{
    memcpy(path, FOLDER "/", sizeof FOLDER);
    uint32_t n = sizeof FOLDER;
    while (*id)
        path[n++] = *id++;
    memcpy(path + n, SUFFIX, sizeof SUFFIX);
}

static int theme_file_open(const char *id, struct rv_file **file)
{
    char path[sizeof FOLDER + RVT_ID_BYTES + sizeof SUFFIX];
    path_of(id, path);
    return api->content_open(self, path, file) == RV_OK;
}

int look_file_open(const struct look *look, struct rv_file **file)
{
    if (look->id[0])
        return theme_file_open(look->id, file);
    return look->dark && api->asset_open(self, RESERVED_ID, file) == RV_OK;
}

int look_pack_place(struct rv_file *file, struct look *look, struct pack_place *pack)
{
    uint8_t header[RVT_HEADER_BYTES];
    if (!header_read(file, header) ||
        (look->id[0] && memcmp(header + RVT_HEADER_ID, look->id, RVT_ID_BYTES)))
        return 0;
    look->header_crc = le32(header + RVT_HEADER_CRC);
    return pack_place(header, look->dark, pack);
}

int theme_file_matches(const char *id, uint32_t crc)
{
    uint8_t header[RVT_HEADER_BYTES];
    struct rv_file *file;
    if (!theme_file_open(id, &file))
        return 0;
    int same = header_read(file, header) && le32(header + RVT_HEADER_CRC) == crc;
    api->file_close(self, file);
    return same;
}

static void count_entry(const char *entry, int is_folder, void *arg)
{
    char id[RVT_ID_BYTES];
    if (!is_folder && stem_id(entry, id))
        ++*(uint32_t *)arg;
}

uint32_t theme_count(void)
{
    uint32_t n = 0;
    api->content_list(self, FOLDER, count_entry, &n);
    return n;
}

struct scan {
    struct theme_entry *out;
    uint32_t capacity, count;
    int overflow;
};

/* Collects ids in order while the folder is listed; files are opened after. */
static void collect(const char *entry, int is_folder, void *arg)
{
    struct scan *s = arg;
    char id[RVT_ID_BYTES];
    if (is_folder || !stem_id(entry, id))
        return;
    uint32_t at = s->count;
    while (at && memcmp(s->out[at - 1].id, id, RVT_ID_BYTES) > 0)
        at--;
    if (at && !memcmp(s->out[at - 1].id, id, RVT_ID_BYTES))
        return;
    if (s->count == s->capacity) {
        s->overflow = 1;
        if (at == s->capacity)
            return;
        s->count--;
    }
    memmove(s->out + at + 1, s->out + at, (s->count - at) * sizeof *s->out);
    memcpy(s->out[at].id, id, RVT_ID_BYTES);
    s->count++;
}

uint32_t theme_scan(struct theme_entry *out, uint32_t capacity, int *overflow)
{
    struct scan s = {out, capacity, 0, 0};
    struct look look;
    struct rv_file *file;
    uint8_t header[RVT_HEADER_BYTES];
    api->content_list(self, FOLDER, collect, &s);
    uint32_t kept = 0;
    for (uint32_t i = 0; i < s.count; i++) {
        if (!theme_file_open(out[i].id, &file))
            continue;
        int ok = header_read(file, header) && header_look(header, out[i].id, &look);
        api->file_close(self, file);
        if (!ok)
            continue;
        memcpy(out[kept].id, look.id, RVT_ID_BYTES);
        memcpy(out[kept].name, look.name, RVT_NAME_BYTES);
        kept++;
    }
    *overflow = s.overflow;
    return kept;
}

enum theme_result theme_open(const char *id, int dark, struct rv_file **file, struct look *look,
                             struct pack_place *pack)
{
    uint8_t header[RVT_HEADER_BYTES];
    enum theme_result result = THEME_OK;
    *file = NULL;
    if (!id_valid(id) || !theme_file_open(id, file))
        return THEME_UNAVAILABLE;
    if (header_read(*file, header) && header_look(header, id, look)) {
        if (!(look->appearances & (dark ? RVT_APPEARANCE_DARK : RVT_APPEARANCE_LIGHT))) {
            dark = !dark;
            result = THEME_NOT_IN_APPEARANCE;
        }
        look->dark = dark;
        if (pack_place(header, dark, pack))
            return result;
    }
    api->file_close(self, *file);
    *file = NULL;
    return THEME_UNAVAILABLE;
}
