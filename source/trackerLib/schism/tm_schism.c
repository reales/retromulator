/* Trackermeister: one Schism song_t per instance, rendered to float. */

#include "include/headers.h"
#include "include/slurp.h"
#include "include/fmt.h"
#include "include/player/sndfile.h"
#include "include/player/snd_fm.h"
#include "tm_schism.h"

#define TM_BLOCK 1024

struct tm_schism {
	song_t *csf;
	bool playing;
	int orders, channels;
	float vus[MAX_CHANNELS];
	int32_t block[TM_BLOCK * 2];
};

static void tm_slurp_memory(slurp_t *s, const uint8_t *data, size_t size)
{
	memset(s, 0, sizeof(*s));
	s->internal.memory.data = data;
	s->internal.memory.length = size;
}

tm_schism_t *tmSchismCreate(const uint8_t *data, size_t size, uint32_t freq)
{
	tm_schism_t *t;
	slurp_t s;
	int n, used;

	if (!data || size < 96)
		return NULL;

	t = (tm_schism_t *)calloc(1, sizeof(*t));
	if (!t)
		return NULL;

	t->csf = csf_allocate();
	t->csf->max_voices = MAX_VOICES;
	t->csf->tm_tempo_scale = 1.0;
	csf_set_wave_config(t->csf, freq, 32, 2);
	csf_set_resampling_mode(t->csf, SRCMODE_POLYPHASE);

	tm_slurp_memory(&s, data, size);
	n = (size >= 4 && memcmp(data, "IMPM", 4) == 0)
		? fmt_it_load_song(t->csf, &s, 0)
		: fmt_s3m_load_song(t->csf, &s, 0);
	if (n != LOAD_SUCCESS) {
		tmSchismDestroy(t);
		return NULL;
	}

	t->csf->repeat_count = 0;
	csf_forget_history(t->csf);

	for (n = 0; n < MAX_ORDERS && t->csf->orderlist[n] != ORDER_LAST; n++) {}
	t->orders = n;

	for (n = MAX_CHANNELS; n > 0; n--) {
		if (!(t->csf->channels[n - 1].flags & CHN_MUTE))
			break;
	}
	t->channels = n;

	/* IT leaves all 64 channels enabled: count the ones the patterns use. */
	for (used = 0, n = 0; n < MAX_PATTERNS; n++) {
		const song_note_t *p = t->csf->patterns[n];
		int row, ch;
		if (!p)
			continue;
		for (row = 0; row < t->csf->pattern_size[n]; row++, p += MAX_CHANNELS) {
			for (ch = MAX_CHANNELS; ch > used; ch--) {
				if (p[ch - 1].note || p[ch - 1].instrument || p[ch - 1].voleffect || p[ch - 1].effect) {
					used = ch;
					break;
				}
			}
		}
	}
	if (used && used < t->channels)
		t->channels = used;

	return t;
}

void tmSchismDestroy(tm_schism_t *t)
{
	if (!t)
		return;
	if (t->csf) {
		OPL_Close(t->csf); /* csf_free leaves the chip alone */
		csf_free(t->csf);
	}
	free(t);
}

void tmSchismSetSampleRate(tm_schism_t *t, uint32_t freq)
{
	if (t && freq && t->csf->mix_frequency != freq)
		csf_set_wave_config(t->csf, freq, 32, 2);
}

void tmSchismSetSinc512(tm_schism_t *t, bool enabled)
{
	if (t)
		csf_set_resampling_mode(t->csf, enabled ? SRCMODE_SINC256 : SRCMODE_POLYPHASE);
}

void tmSchismSetTempoScale(tm_schism_t *t, double scale)
{
	if (t && scale > 0.0)
		t->csf->tm_tempo_scale = scale;
}

void tmSchismPlay(tm_schism_t *t, int order, bool stopAtEnd)
{
	song_t *csf;

	if (!t)
		return;
	csf = t->csf;

	if (order < 0) order = 0;
	if (t->orders > 0 && order >= t->orders) order = t->orders - 1;

	csf->mix_flags &= ~(SNDMIX_NOBACKWARDJUMPS | SNDMIX_DIRECTTODISK);
	if (stopAtEnd)
		csf->mix_flags |= SNDMIX_NOBACKWARDJUMPS;

	OPL_Reset(csf);
	csf_set_current_order(csf, 0);
	csf->repeat_count = stopAtEnd ? -1 : 0;
	csf->buffer_count = 0;
	csf->tm_tick_frac = 0.0;
	csf->flags &= ~(SONG_PAUSED | SONG_PATTERNLOOP | SONG_ENDREACHED);
	csf->stop_at_order = -1;
	csf->stop_at_row = -1;

	csf_set_current_order(csf, (uint32_t)order);
	csf_reset_playmarks(csf);
	t->playing = true;
}

void tmSchismStop(tm_schism_t *t)
{
	uint32_t n;

	if (!t)
		return;

	t->playing = false;
	OPL_Reset(t->csf);
	for (n = 0; n < MAX_VOICES; n++) {
		t->csf->voices[n].length = 0;
		t->csf->voices[n].current_sample_data = NULL;
		t->csf->voices[n].rofs = t->csf->voices[n].lofs = 0;
	}
	t->csf->dry_rofs_vol = t->csf->dry_lofs_vol = 0;
}

void tmSchismRender(tm_schism_t *t, float *out, uint32_t frames)
{
	const float scale = 1.0f / 2147483648.0f;

	if (!t || !t->playing) {
		memset(out, 0, (size_t)frames * 2 * sizeof(float));
		return;
	}

	while (frames > 0) {
		uint32_t n = (frames > TM_BLOCK) ? TM_BLOCK : frames;
		uint32_t got = csf_read(t->csf, t->block, n * 2 * sizeof(int32_t));
		uint32_t i;

		for (i = 0; i < got * 2; i++)
			out[i] = (float)t->block[i] * scale;
		for (; i < n * 2; i++)
			out[i] = 0.0f;

		out += n * 2;
		frames -= n;
	}
}

bool tmSchismHasEnded(const tm_schism_t *t)
{
	return t && (t->csf->flags & SONG_ENDREACHED) != 0;
}

int tmSchismGetOrder(const tm_schism_t *t) { return t ? (int)t->csf->current_order : 0; }
int tmSchismGetRow(const tm_schism_t *t) { return t ? (int)t->csf->row : 0; }
int tmSchismGetOrderCount(const tm_schism_t *t) { return t ? t->orders : 0; }
int tmSchismGetChannelCount(const tm_schism_t *t) { return t ? t->channels : 0; }
int tmSchismGetInitialBpm(const tm_schism_t *t) { return t ? (int)t->csf->initial_tempo : 125; }
const char *tmSchismGetTitle(const tm_schism_t *t) { return t ? t->csf->title : ""; }

float tmSchismGetChannelLevel(tm_schism_t *t, int channel)
{
	if (!t || channel < 0 || channel >= MAX_CHANNELS)
		return 0.0f;
	if (channel == 0)
		csf_calculate_vu_meters(t->csf, t->vus);
	return t->vus[channel] > 1.0f ? 1.0f : t->vus[channel];
}
