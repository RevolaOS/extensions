/* SPDX-License-Identifier: GPL-3.0-only */

/* The same approach as RepriseOS pull request 63 by kayboll, arrived at
 * independently. */

/* Apple's MP4 parser seeks by stepping through every sample from its current
 * position, or from the first sample when the target lies behind, until the
 * presentation time reaches the target: about 155 instructions per AAC frame and
 * a 128 KB table read every 32,768 frames. This handler runs at the entry of
 * that walk (MP4Parser_SeekAudioTrack), works out from the time-to-sample and
 * sample-to-chunk runs which chunk holds the target, moves the track state to
 * that chunk's first sample and lets Apple's loop finish the chunk, loading the
 * table windows as it always does. Tables outside the plain shape (several
 * edits, several time-to-sample runs before the target, windows not at the
 * table start) leave the walk to Apple. */

#include "osos-2.0.5.h"
#include "rv_api.h"
#include "rv_sites.h"
#include "rv_ui.h"

static const struct rv_api *api;

static uint32_t word(const void *base, uint32_t offset)
{
    return RV_FIELD(base, offset, uint32_t);
}

static uint64_t pair(const void *base, uint32_t offset)
{
    return (uint64_t)word(base, offset + 4) << 32 | word(base, offset);
}

static void set_pair(void *base, uint32_t offset, uint64_t value)
{
    RV_FIELD(base, offset, uint32_t) = (uint32_t)value;
    RV_FIELD(base, offset + 4, uint32_t) = (uint32_t)(value >> 32);
}

/* a × b for b below 2^16 (timescales and frame lengths), without the 64-bit
 * multiply helper. */
static uint64_t mul(uint32_t a, uint32_t b)
{
    uint32_t low = (a & 0xffff) * b, high = (a >> 16) * b;
    return ((uint64_t)(high >> 16) << 32) + ((uint64_t)(high & 0xffff) << 16) + low;
}

/* Move the audio track to the first sample of the chunk that holds the seek
 * target, when the tables allow it. Samples and chunks are 1-based, as in the
 * track state. */
static void position(uint8_t *parser, uint32_t ms)
{
    uint8_t *track = RV_FIELD(parser, MP4Parser_audio_track, uint8_t *);
    /* Apple's walk returns at once for a track without a chunk offset atom. */
    if (!track || pair(track, MP4Track_chunk_offset_atom) == UINT64_MAX)
        return;
    const uint8_t *sizes = RV_FIELD(track, MP4Track_sample_sizes, const uint8_t *);
    const uint8_t *chunks = RV_FIELD(track, MP4Track_chunk_offsets, const uint8_t *);
    const uint8_t *runs = RV_FIELD(track, MP4Track_sample_to_chunk, const uint8_t *);
    const uint8_t *times = RV_FIELD(track, MP4Track_time_to_sample, const uint8_t *);
    const uint8_t *edits = RV_FIELD(track, MP4Track_edits, const uint8_t *);
    uint32_t timescale = word(track, MP4Track_timescale);
    if (!sizes || !chunks || !runs || !times || !timescale || timescale > 0xffff)
        return;
    uint32_t run_count = word(runs, MP4SampleTable_count);
    if (word(times, MP4SampleTable_window_first) || !word(times, MP4SampleTable_count) ||
        word(runs, MP4SampleTable_window_first) || !run_count ||
        run_count > MP4_SampleToChunkWindow)
        return;
    /* One edit, already the current one: presentation time starts at its media start. */
    uint64_t edit_start = 0;
    if (edits) {
        if (word(edits, MP4EditList_count) != 1 || word(track, MP4Track_edit_index))
            return;
        edit_start = pair(track, MP4Track_edit_media_start);
        if (edit_start == UINT64_MAX)
            return;
    }
    /* Only the first time-to-sample run; a target past it stays with Apple. */
    const uint8_t *time = RV_FIELD(times, MP4SampleTable_window, const uint8_t *);
    uint32_t delta = word(time, MP4TimeToSampleEntry_delta);
    uint32_t in_run = word(time, MP4TimeToSampleEntry_count);
    if (!delta || delta > 0xffff)
        return;
    /* The first sample inside the edit, and the sample at which Apple's loop stops,
     * rounded down: the loop can only move forward from where it is put. */
    uint64_t target = Runtime_UDiv64(mul(ms, timescale), 1000);
    uint64_t first = Runtime_UDiv64(edit_start + delta - 1, delta) + 1;
    uint64_t stop = first - 1 + Runtime_UDiv64(target, delta);
    if (stop < 1 || stop > in_run || stop > word(sizes, MP4SampleSizeTable_count))
        return;
    /* The chunk holding `stop`, from the sample-to-chunk runs. */
    const uint8_t *run = RV_FIELD(runs, MP4SampleTable_window, const uint8_t *);
    uint32_t chunk_count = word(chunks, MP4SampleTable_count);
    uint32_t index = (uint32_t)stop - 1, before = 0, run_index = 0, chunk, chunk_first;
    for (;; run_index++, run += MP4SampleToChunkEntry_bytes) {
        uint32_t from = word(run, MP4SampleToChunkEntry_first_chunk);
        uint32_t per = word(run, MP4SampleToChunkEntry_samples_per_chunk);
        uint32_t until = run_index + 1 < run_count
                             ? word(run + MP4SampleToChunkEntry_bytes,
                                    MP4SampleToChunkEntry_first_chunk)
                             : chunk_count + 1;
        if (!per || until <= from)
            return;
        uint32_t span = (until - from) * per;
        if (index - before < span) {
            chunk = from + (index - before) / per;
            chunk_first = before + (chunk - from) * per + 1;
            break;
        }
        before += span;
        if (run_index + 1 >= run_count)
            return;
    }
    /* Already inside the stretch Apple would walk forward: leave it. */
    uint32_t sample = word(track, MP4Track_sample);
    if (sample >= chunk_first && sample <= stop)
        return;
    RV_FIELD(track, MP4Track_sample, uint32_t) = chunk_first;
    RV_FIELD(track, MP4Track_sample_to_chunk_index, uint32_t) = run_index;
    RV_FIELD(track, MP4Track_chunk, uint32_t) = chunk;
    RV_FIELD(track, MP4Track_sample_in_chunk, uint32_t) = 0;
    RV_FIELD(track, MP4Track_chunk_byte_offset, uint32_t) = 0;
    RV_FIELD(track, MP4Track_time_to_sample_index, uint32_t) = 0;
    RV_FIELD(track, MP4Track_time_to_sample_run, uint32_t) = chunk_first - 1;
    set_pair(track, MP4Track_decode_time, mul(chunk_first - 1, delta));
    set_pair(track, MP4Track_presentation_time,
             chunk_first > first ? mul(chunk_first - (uint32_t)first, delta) : 0);
}

static uint32_t on_seek(uint8_t *parser, uint32_t ms, const void *next)
{
    position(parser, ms);
    return api->hook_next(next, (uint32_t)parser, ms, 0, 0, 0, 0);
}

static const struct rv_hook hooks[] = {
    RV_HOOK_FILTER(RV_SITE_MP4Parser_SeekAudioTrack_entry, on_seek),
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
