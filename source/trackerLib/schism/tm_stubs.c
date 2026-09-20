/* Trackermeister glue for the Schism Tracker player: everything the player
 * and the S3M and IT loaders pull in from the rest of Schism, cut down to playback. */

#include "include/headers.h"
#include "include/slurp.h"
#include "include/mem.h"
#include "include/log.h"
#include "include/fmt.h"
#include "include/midi.h"
#include "include/disko.h"
#include "include/song.h"
#include "include/timer.h"
#include "include/version.h"
#include "include/charset.h"
#include "include/str.h"
#include "include/bits.h"
#include "include/player/sndfile.h"
#include "include/player/cmixer.h"
#include "include/player/snd_gm.h"
#include "include/player/fmopl.h"

#include "../../opl3Lib/opl3.h"

/* ---- settings the player reads ---- */

struct audio_settings audio_settings;
int midi_flags = 0;

/* ---- memory ---- */

void *mem_alloc(size_t amount)
{
	void *q = malloc(amount ? amount : 1);
	if (!q) abort();
	return q;
}

void *mem_calloc(size_t nmemb, size_t size)
{
	void *q = calloc(nmemb ? nmemb : 1, size ? size : 1);
	if (!q) abort();
	return q;
}

void *mem_realloc(void *orig, size_t amount)
{
	void *q = realloc(orig, amount ? amount : 1);
	if (!q) abort();
	return q;
}

char *strn_dup(const char *s, size_t n)
{
	char *q = (char *)mem_alloc(n + 1);
	memcpy(q, s, n);
	q[n] = '\0';
	return q;
}

void mem_xor(void *vbuf, size_t len, unsigned char c)
{
	unsigned char *buf = (unsigned char *)vbuf;
	while (len--) *buf++ ^= c;
}

/* ---- logging and asserts ---- */

void log_appendf(int color, const char *format, ...)
{
	(void)color; (void)format;
}

void schism_assert_fail(const char *msg, const char *exp, const char *file, int line)
{
	fprintf(stderr, "schism assert: %s (%s) %s:%d\n", msg ? msg : "", exp, file, line);
	abort();
}

/* ---- memory-only slurp ---- */

int slurp_seek(slurp_t *t, int64_t offset, int whence)
{
	int64_t base;
	switch (whence) {
	case SEEK_SET: base = 0; break;
	case SEEK_CUR: base = (int64_t)t->internal.memory.pos; break;
	case SEEK_END: base = (int64_t)t->internal.memory.length; break;
	default: return -1;
	}
	base += offset;
	if (base < 0 || base > (int64_t)t->internal.memory.length)
		return -1;
	t->internal.memory.pos = (size_t)base;
	t->eof_ = 0;
	return 0;
}

int64_t slurp_tell(slurp_t *t)
{
	return (int64_t)t->internal.memory.pos;
}

size_t slurp_read(slurp_t *t, void *ptr, size_t count)
{
	size_t left = t->internal.memory.length - t->internal.memory.pos;
	size_t n = (count < left) ? count : left;
	if (n) memcpy(ptr, t->internal.memory.data + t->internal.memory.pos, n);
	/* short reads come back zero-filled, like the original */
	if (n < count) {
		memset((unsigned char *)ptr + n, 0, count - n);
		t->eof_ = 1;
	}
	t->internal.memory.pos += n;
	return n;
}

int slurp_getc(slurp_t *t)
{
	unsigned char c;
	return (slurp_read(t, &c, 1) == 1) ? (int)c : EOF;
}

int slurp_available(slurp_t *t, size_t x, int whence)
{
	int64_t pos;
	switch (whence) {
	case SEEK_SET: pos = 0; break;
	case SEEK_CUR: pos = (int64_t)t->internal.memory.pos; break;
	default: return 0;
	}
	return (pos + (int64_t)x) <= (int64_t)t->internal.memory.length;
}

int slurp_eof(slurp_t *t)
{
	return t->eof_;
}

/* ---- sample decoders the S3M and IT loaders never reach ---- */

uint32_t mdl_decompress8(void *dest, uint32_t len, slurp_t *fp)
{ (void)dest; (void)len; (void)fp; return 0; }
uint32_t mdl_decompress16(void *dest, uint32_t len, slurp_t *fp)
{ (void)dest; (void)len; (void)fp; return 0; }
double float_decode_ieee_32(const unsigned char bytes[4]) { (void)bytes; return 0.0; }
double float_decode_ieee_64(const unsigned char bytes[8]) { (void)bytes; return 0.0; }

/* ---- IT loader helpers ---- */

int midi_pitch_depth = 12;

int str_rtrim(char *s)
{
	int len = (int)strlen(s);
	while (len > 0 && isspace((unsigned char)s[len - 1])) len--;
	s[len] = '\0';
	return len;
}

/* Titles stay in the file's own encoding. */
charset_error_t charset_iconv(const void *in, void *out, charset_t inset, charset_t outset, size_t insize)
{ (void)in; (void)out; (void)inset; (void)outset; (void)insize; return CHARSET_ERROR_UNIMPLEMENTED; }

int instrument_loader_sample(struct instrumentloader *ii, int slot) { (void)ii; return slot; }

void fat_date_time_to_tm(struct tm *tm, uint16_t fat_date, uint16_t fat_time)
{ (void)fat_date; (void)fat_time; memset(tm, 0, sizeof(*tm)); }

#ifdef _MSC_VER
struct tm *localtime_r(const time_t *timep, struct tm *result)
{ return localtime_s(result, timep) == 0 ? result : NULL; }
#endif

/* Songs saved by an older Schism keep that version's playback quirks. */
void fmt_fill_schism_quirks(song_t *csf, uint32_t ver)
{
	static const struct { uint32_t verfixed; int quirk; } quirks[] = {
		{0x079a, CSF_QUIRK_PERIODS_ARE_HERTZ},
		{0x0970, CSF_QUIRK_IT_SHORT_SAMPLE_RETRIG},
		{0x1087, CSF_QUIRK_IT_DO_NOT_OVERRIDE_CHANNEL_PAN},
		{0x1087, CSF_QUIRK_IT_PANNING_RESET},
		{0x113e, CSF_QUIRK_IT_PITCH_PAN_SEPARATION},
		{0x11f2, CSF_QUIRK_IT_EMPTY_NOTE_MAP_SLOT},
		{0x11f2, CSF_QUIRK_IT_PORTAMENTO_SWAP_RESETS_POSITION},
		{0x11f2, CSF_QUIRK_IT_MULTI_SAMPLE_INSTRUMENT_NUMBER},
		{0x132b, CSF_QUIRK_IT_INITIAL_NOTE_MEMORY},
		{0x1409, CSF_QUIRK_IT_DCT_BEHAVIOR},
		{0x140b, CSF_QUIRK_IT_SAMPLE_AND_HOLD_PANBRELLO},
		{0x140b, CSF_QUIRK_IT_PORTAMENTO_NO_NOTE},
		{0x140e, CSF_QUIRK_IT_FIRST_TICK_HANDLING},
		{0x140e, CSF_QUIRK_IT_MULTI_SAMPLE_INSTRUMENT_NUMBER},
		{0x1499, CSF_QUIRK_IT_PANBRELLO_HOLD},
		{0x14d9, CSF_QUIRK_IT_NO_SUSTAIN_ON_PORTAMENTO},
		{0x14d9, CSF_QUIRK_IT_EMPTY_NOTE_MAP_SLOT_IGNORE_CELL},
		{0x14e8, CSF_QUIRK_IT_OFFSET_WITH_INSTRUMENT_NUMBER},
		{0x1573, CSF_QUIRK_IT_DOUBLE_PORTAMENTO_SLIDES},
		{0x15ca, CSF_QUIRK_IT_CARRY_AFTER_NOTE_OFF},
	};
	size_t i;

	for (i = 0; i < ARRAY_SIZE(quirks); i++)
		if (quirks[i].verfixed >= ver)
			BITARRAY_CLEAR(csf->quirks, quirks[i].quirk);
}

/* ---- edit-time and tracker-id bookkeeping, unused for playback ---- */

timer_ticks_t timer_ticks(void) { return 0; }
timer_ticks_t dos_time_to_ms(uint32_t dos_time) { (void)dos_time; return 0; }
uint32_t it_decode_edit_timer(uint16_t cwtv, uint32_t runtime) { (void)cwtv; return runtime; }
uint32_t ver_mktime(uint32_t year, uint32_t month, uint32_t day)
{ (void)year; (void)month; (void)day; return 0; }
void ver_decode_cwtv(uint16_t cwtv, uint32_t reserved, char buf[11])
{ (void)cwtv; (void)reserved; buf[0] = '\0'; }

/* ---- sample writer, never called ---- */

void disko_write(disko_t *ds, const void *buf, size_t len) { (void)ds; (void)buf; (void)len; }

/* ---- equalizer and master volume: flat ---- */

void normalize_mono(song_t *csf, int32_t *buffer, uint32_t samples) { (void)csf; (void)buffer; (void)samples; }
void normalize_stereo(song_t *csf, int32_t *buffer, uint32_t samples) { (void)csf; (void)buffer; (void)samples; }
void eq_mono(song_t *csf, int32_t *buffer, uint32_t count) { (void)csf; (void)buffer; (void)count; }
void eq_stereo(song_t *csf, int32_t *buffer, uint32_t count) { (void)csf; (void)buffer; (void)count; }
void song_init_eq(int do_reset, uint32_t mix_freq) { (void)do_reset; (void)mix_freq; }

/* ---- MIDI out: not wired ---- */

uint8_t midi_event_length(uint8_t first_byte) { (void)first_byte; return 1; }
void GM_DPatch(song_t *csf, int32_t ch, unsigned char GM, unsigned char bank, int32_t pref_chn_mask)
{ (void)csf; (void)ch; (void)GM; (void)bank; (void)pref_chn_mask; }
void GM_Touch(song_t *csf, int32_t c, unsigned char Vol) { (void)csf; (void)c; (void)Vol; }
void GM_KeyOff(song_t *csf, int32_t c) { (void)csf; (void)c; }
void GM_Reset(song_t *csf, int quitting) { (void)csf; (void)quitting; }
void GM_Pan(song_t *csf, int32_t ch, signed char val) { (void)csf; (void)ch; (void)val; }
void GM_SetFreqAndVol(song_t *csf, int32_t channel, int32_t Hertz, int32_t Vol, MidiBendMode bend_mode, int32_t keyoff)
{ (void)csf; (void)channel; (void)Hertz; (void)Vol; (void)bend_mode; (void)keyoff; }
void GM_IncrementSongCounter(song_t *csf, int32_t count) { (void)csf; (void)count; }

/* ---- Adlib: Schism's FM driver on top of Nuked OPL3 ---- */

typedef struct {
	opl3_chip chip;
	uint32_t rate;
	uint16_t address;
	int16_t scratch[MIXBUFFERSIZE * 2];
} tm_opl_t;

void *ymf262_init(uint32_t clock, uint32_t rate)
{
	tm_opl_t *o = (tm_opl_t *)calloc(1, sizeof(tm_opl_t));
	(void)clock;
	if (!o) return NULL;
	o->rate = rate;
	OPL3_Reset(&o->chip, rate);
	return o;
}

void ymf262_shutdown(void *chip)
{
	free(chip);
}

void ymf262_reset_chip(void *chip)
{
	tm_opl_t *o = (tm_opl_t *)chip;
	OPL3_Reset(&o->chip, o->rate);
	o->address = 0;
}

int ymf262_write(void *chip, int a, int v)
{
	tm_opl_t *o = (tm_opl_t *)chip;
	if (a & 1)
		OPL3_WriteReg(&o->chip, o->address, (uint8_t)v);
	else
		o->address = (uint16_t)((v & 0xFF) | ((a & 2) ? 0x100 : 0));
	return 0;
}

unsigned char ymf262_read(void *chip, int a)
{
	(void)chip; (void)a;
	return 0;
}

/* Nuked mixes all 18 channels itself, so the sum goes to the first live buffer
 * and the per-channel meters all follow the chip peak. */
void ymf262_update_multi(void *chip, int32_t **buffers, int length, uint32_t vu_max[OPL_CHANNELS])
{
	tm_opl_t *o = (tm_opl_t *)chip;
	int32_t *dst = NULL;
	uint32_t peak = 0;
	int i, j;

	for (j = 0; j < OPL_CHANNELS; j++) {
		if (buffers[j]) { dst = buffers[j]; break; }
	}

	while (length > 0) {
		int n = (length > MIXBUFFERSIZE) ? MIXBUFFERSIZE : length;
		OPL3_GenerateStream(&o->chip, o->scratch, (uint32_t)n);

		for (i = 0; i < n * 2; i++) {
			int32_t s = o->scratch[i];
			uint32_t a = (uint32_t)(s < 0 ? -s : s);
			if (a > peak) peak = a;
			if (dst) dst[i] += s * OPL_VOLUME;
		}

		if (dst) dst += n * 2;
		length -= n;
	}

	for (j = 0; j < OPL_CHANNELS; j++) {
		if (buffers[j] && peak > vu_max[j]) vu_max[j] = peak;
	}
}
