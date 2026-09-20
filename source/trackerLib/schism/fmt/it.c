/*
 * Schism Tracker - a cross-platform Impulse Tracker clone
 * copyright (c) 2003-2005 Storlek <storlek@rigelseven.com>
 * copyright (c) 2005-2008 Mrs. Brisby <mrs.brisby@nimh.org>
 * copyright (c) 2009 Storlek & Mrs. Brisby
 * copyright (c) 2010-2012 Storlek
 * URL: http://schismtracker.org/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "../include/headers.h"
#include "../include/bits.h"
#include "../include/charset.h"
#include "../include/slurp.h"
#include "../include/fmt.h"
#include "../include/log.h"
#include "../include/version.h"
#include "../include/mem.h"
#include "../include/str.h"

#include "../include/player/sndfile.h"
#include "../include/midi.h"

/* --------------------------------------------------------------------- */

#define IT_CHANNELS 64

#if IT_CHANNELS != MAX_CHANNELS
# error The code currently assumes IT_CHANNELS == MAX_CHANNELS. If they need to differ, then many assumptions will need to be rewritten. IT files always have 64 channels.
#endif

struct it_file {
	uint32_t id;                    // 0x4D504D49
	int8_t songname[26];
	uint8_t hilight_minor;
	uint8_t hilight_major;
	uint16_t ordnum;
	uint16_t insnum;
	uint16_t smpnum;
	uint16_t patnum;
	uint16_t cwtv;
	uint16_t cmwt;
	uint16_t flags;
	uint16_t special;
	uint8_t globalvol;
	uint8_t mv;
	uint8_t speed;
	uint8_t tempo;
	uint8_t sep;
	uint8_t pwd;
	uint16_t msglength;
	uint32_t msgoffset;
	uint32_t reserved;
	uint8_t chnpan[IT_CHANNELS];
	uint8_t chnvol[IT_CHANNELS];
};

static int it_load_header(struct it_file *hdr, slurp_t *fp)
{
	size_t n;

#define LOAD_VALUE(name) do { if (slurp_read(fp, &hdr->name, sizeof(hdr->name)) != sizeof(hdr->name)) { return 0; } } while (0)

	LOAD_VALUE(id);
	LOAD_VALUE(songname);
	LOAD_VALUE(hilight_minor);
	LOAD_VALUE(hilight_major);
	LOAD_VALUE(ordnum);
	LOAD_VALUE(insnum);
	LOAD_VALUE(smpnum);
	LOAD_VALUE(patnum);
	LOAD_VALUE(cwtv);
	LOAD_VALUE(cmwt);
	LOAD_VALUE(flags);
	LOAD_VALUE(special);
	LOAD_VALUE(globalvol);
	LOAD_VALUE(mv);
	LOAD_VALUE(speed);
	LOAD_VALUE(tempo);
	LOAD_VALUE(sep);
	LOAD_VALUE(pwd);
	LOAD_VALUE(msglength);
	LOAD_VALUE(msgoffset);
	LOAD_VALUE(reserved);
	LOAD_VALUE(chnpan);
	LOAD_VALUE(chnvol);

#undef LOAD_VALUE

	if (memcmp(&hdr->id, "IMPM", 4))
		return 0;

	hdr->ordnum = bswapLE16(hdr->ordnum);
	hdr->insnum = bswapLE16(hdr->insnum);
	hdr->smpnum = bswapLE16(hdr->smpnum);
	hdr->patnum = bswapLE16(hdr->patnum);
	hdr->cwtv = bswapLE16(hdr->cwtv);
	hdr->cmwt = bswapLE16(hdr->cmwt);
	hdr->flags = bswapLE16(hdr->flags);
	hdr->special = bswapLE16(hdr->special);
	hdr->msglength = bswapLE16(hdr->msglength);
	hdr->msgoffset = bswapLE32(hdr->msgoffset);
	hdr->reserved = bswapLE32(hdr->reserved);

	/* replace NUL bytes with spaces */
	for (n = 0; n < sizeof(hdr->songname); n++)
		if (!hdr->songname[n])
			hdr->songname[n] = 0x20;

	return 1;
}

/* --------------------------------------------------------------------- */

/* pattern mask variable bits */
enum {
	ITNOTE_NOTE = 1,
	ITNOTE_SAMPLE = 2,
	ITNOTE_VOLUME = 4,
	ITNOTE_EFFECT = 8,
	ITNOTE_SAME_NOTE = 16,
	ITNOTE_SAME_SAMPLE = 32,
	ITNOTE_SAME_VOLUME = 64,
	ITNOTE_SAME_EFFECT = 128,
};

/* --------------------------------------------------------------------- */

static void it_import_voleffect(song_note_t *note, uint8_t v)
{
	uint8_t adj;

	if (v <= 64)                   { adj =   0; note->voleffect = VOLFX_VOLUME; }
	else if (v >= 128 && v <= 192) { adj = 128; note->voleffect = VOLFX_PANNING; }
	else if (v >= 65 && v <= 74)   { adj =  65; note->voleffect = VOLFX_FINEVOLUP; }
	else if (v >= 75 && v <= 84)   { adj =  75; note->voleffect = VOLFX_FINEVOLDOWN; }
	else if (v >= 85 && v <= 94)   { adj =  85; note->voleffect = VOLFX_VOLSLIDEUP; }
	else if (v >= 95 && v <= 104)  { adj =  95; note->voleffect = VOLFX_VOLSLIDEDOWN; }
	else if (v >= 105 && v <= 114) { adj = 105; note->voleffect = VOLFX_PORTADOWN; }
	else if (v >= 115 && v <= 124) { adj = 115; note->voleffect = VOLFX_PORTAUP; }
	else if (v >= 193 && v <= 202) { adj = 193; note->voleffect = VOLFX_TONEPORTAMENTO; }
	else if (v >= 203 && v <= 212) { adj = 203; note->voleffect = VOLFX_VIBRATODEPTH; }
	else { return; }

	note->volparam = v - adj;
}

static void load_it_pattern(song_note_t *note, slurp_t *fp, int rows, uint16_t cwtv)
{
	song_note_t last_note[IT_CHANNELS];
	int chan, row = 0;
	uint8_t last_mask[IT_CHANNELS] = { 0 };
	uint8_t chanvar, maskvar, c;

	while (row < rows) {
		chanvar = slurp_getc(fp);

		if (chanvar == 255 && slurp_eof(fp)) {
			/* truncated file? we might want to complain or something ... eh. */
			return;
		}

		if (chanvar == 0) {
			row++;
			note += 64;
			continue;
		}
		chan = (chanvar - 1) % IT_CHANNELS;
		if (chanvar & 128) {
			maskvar = slurp_getc(fp);
			last_mask[chan] = maskvar;
		} else {
			maskvar = last_mask[chan];
		}
		if (maskvar & ITNOTE_NOTE) {
			c = slurp_getc(fp);
			if (c == 255)
				c = NOTE_OFF;
			else if (c == 254)
				c = NOTE_CUT;
			// internally IT uses note 253 as its blank value, but loading it as such is probably
			// undesirable since old Schism Tracker used this value incorrectly for note fade
			//else if (c == 253)
			//      c = NOTE_NONE;
			else if (c > 119)
				c = NOTE_FADE;
			else
				c += NOTE_FIRST;
			note[chan].note = c;
			last_note[chan].note = note[chan].note;
		}
		if (maskvar & ITNOTE_SAMPLE) {
			note[chan].instrument = slurp_getc(fp);
			last_note[chan].instrument = note[chan].instrument;
		}
		if (maskvar & ITNOTE_VOLUME) {
			it_import_voleffect(note + chan, slurp_getc(fp));
			last_note[chan].voleffect = note[chan].voleffect;
			last_note[chan].volparam = note[chan].volparam;
		}
		if (maskvar & ITNOTE_EFFECT) {
			note[chan].effect = slurp_getc(fp) & 0x1f;
			note[chan].param = slurp_getc(fp);
			csf_import_s3m_effect(note + chan, 1);

			if (note[chan].effect == FX_SPECIAL && (note[chan].param & 0xf0) == 0xa0 && cwtv < 0x0200) {
				// IT 1.xx does not support high offset command
				note[chan].effect = FX_NONE;
			} else if (note[chan].effect == FX_GLOBALVOLUME && note[chan].param > 0x80 && cwtv >= 0x1000 && cwtv <= 0x1050) {
				// Fix handling of commands V81-VFF in ITs made with old Schism Tracker versions
				// (fixed in commit ab5517d4730d4c717f7ebffb401445679bd30888 - one of the last versions to identify as v0.50)
				note[chan].param = 0x80;
			}

			last_note[chan].effect = note[chan].effect;
			last_note[chan].param = note[chan].param;
		}
		if (maskvar & ITNOTE_SAME_NOTE)
			note[chan].note = last_note[chan].note;
		if (maskvar & ITNOTE_SAME_SAMPLE)
			note[chan].instrument = last_note[chan].instrument;
		if (maskvar & ITNOTE_SAME_VOLUME) {
			note[chan].voleffect = last_note[chan].voleffect;
			note[chan].volparam = last_note[chan].volparam;
		}
		if (maskvar & ITNOTE_SAME_EFFECT) {
			note[chan].effect = last_note[chan].effect;
			note[chan].param = last_note[chan].param;
		}
	}
}

SCHISM_STATIC_ASSERT(MAX_MIDI_MACRO == 32, "MIDI config reading code assumes macros are exactly 32 bytes");

int it_read_midi_config(midi_config_t *midi, slurp_t *fp)
{
	/* preserving this just for compat with old behavior  --paper */
	if (!slurp_available(fp, 4896, SEEK_CUR))
		return 0;

#define READ_VALUE(x) \
	do { if (slurp_read(fp, midi->x, sizeof(midi->x)) != sizeof(midi->x)) return 0; } while (0)

	/* everything in this structure *should* be word
	 * aligned on basically every platform imaginable,
	 * but its trivial to get around the implementation
	 * defined behavior here if some stupid back-asswards
	 * platform decides to put random padding. */
	READ_VALUE(start);
	READ_VALUE(stop);
	READ_VALUE(tick);
	READ_VALUE(note_on);
	READ_VALUE(note_off);
	READ_VALUE(set_volume);
	READ_VALUE(set_panning);
	READ_VALUE(set_bank);
	READ_VALUE(set_program);
	READ_VALUE(sfx);
	READ_VALUE(zxx);

#undef READ_VALUE

	return 1;
}

int fmt_it_load_song(song_t *song, slurp_t *fp, uint32_t lflags)
{
	struct it_file hdr;
	uint32_t para_smp[MAX_SAMPLES], para_ins[MAX_INSTRUMENTS], para_pat[MAX_PATTERNS], para_min;
	int n;
	int modplug = 0;
	int ignoremidi = 0;
	song_channel_t *channel;
	song_sample_t *sample;
	uint16_t hist = 0; // save history (for IT only)
	const char *tid = NULL;

	if (!it_load_header(&hdr, fp))
		return LOAD_UNSUPPORTED;

	// Screwy limits?
	if (hdr.insnum > MAX_INSTRUMENTS || hdr.smpnum > MAX_SAMPLES
		|| hdr.patnum > MAX_PATTERNS) {
		return LOAD_FORMAT_ERROR;
	}

	/* Trackermeister: songname has no terminator, strncpy ran into the next fields */
	memcpy(song->title, hdr.songname, sizeof(hdr.songname));
	song->title[sizeof(hdr.songname)] = '\0';

	str_rtrim(song->title);

	if (hdr.cmwt < 0x0214 && hdr.cwtv < 0x0214)
		ignoremidi = 1;
	if (hdr.special & 4) {
		/* "reserved" bit, experimentally determined to indicate presence of otherwise-documented row
		highlight information - introduced in IT 2.13. Formerly checked cwtv here, but that's lame :)
		XXX does any tracker save highlight but *not* set this bit? (old Schism versions maybe?) */
		song->row_highlight_minor = hdr.hilight_minor;
		song->row_highlight_major = hdr.hilight_major;
	} else {
		song->row_highlight_minor = 4;
		song->row_highlight_major = 16;
	}

	if (!(hdr.flags & 1))
		song->flags |= SONG_NOSTEREO;
	// (hdr.flags & 2) no longer used (was vol0 optimizations)
	if (hdr.flags & 4)
		song->flags |= SONG_INSTRUMENTMODE;
	if (hdr.flags & 8)
		song->flags |= SONG_LINEARSLIDES;
	if (hdr.flags & 16)
		song->flags |= SONG_ITOLDEFFECTS;
	if (hdr.flags & 32)
		song->flags |= SONG_COMPATGXX;
	if (hdr.flags & 64) {
		midi_flags |= MIDI_PITCHBEND;
		midi_pitch_depth = hdr.pwd;
	}
	if ((hdr.flags & 128) && !ignoremidi)
		song->flags |= SONG_EMBEDMIDICFG;
	else
		song->flags &= ~SONG_EMBEDMIDICFG;

	song->initial_global_volume = MIN(hdr.globalvol, 128);
	song->mixing_volume = MIN(hdr.mv, 128);
	song->initial_speed = hdr.speed ? hdr.speed : 6;
	song->initial_tempo = MAX(hdr.tempo, 31);
	song->pan_separation = hdr.sep;

	for (n = 0, channel = song->channels; n < IT_CHANNELS; n++, channel++) {
		int pan = hdr.chnpan[n];
		if (pan & 128) {
			channel->flags |= CHN_MUTE;
			pan &= ~128;
		}
		if (pan == 100) {
			channel->flags |= CHN_SURROUND;
			channel->panning = 32;
		} else {
			channel->panning = MIN(pan, 64);
		}
		channel->panning *= 4; //mphack
		channel->volume = MIN(hdr.chnvol[n], 64);
	}

	/* only read what we can and ignore the rest */
	slurp_read(fp, song->orderlist, MIN(hdr.ordnum, MAX_ORDERS));

	/* show a warning in the message log if there's too many orders */
	if (hdr.ordnum > MAX_ORDERS) {
		const int lostord = hdr.ordnum - MAX_ORDERS;
		int show_warning = 1;

		/* special exception: ordnum == 257 is valid ONLY if the final order is ORDER_LAST */
		if (lostord == 1) {
			uint8_t ord;
			slurp_read(fp, &ord, sizeof(ord));

			show_warning = (ord != ORDER_LAST);
		} else {
			slurp_seek(fp, lostord, SEEK_CUR);
		}

		if (show_warning)
			log_appendf(4, " Warning: Too many orders in the order list (%d skipped)", lostord);
	}

	slurp_read(fp, para_ins, 4 * hdr.insnum);
	slurp_read(fp, para_smp, 4 * hdr.smpnum);
	slurp_read(fp, para_pat, 4 * hdr.patnum);

	para_min = ((hdr.special & 1) && hdr.msglength)
		? hdr.msgoffset
		: UINT32_C(0xFFFFFFFF);
	for (n = 0; n < hdr.insnum; n++) {
		para_ins[n] = bswapLE32(para_ins[n]);
		if (para_ins[n] < para_min)
			para_min = para_ins[n];
	}
	for (n = 0; n < hdr.smpnum; n++) {
		para_smp[n] = bswapLE32(para_smp[n]);
		if (para_smp[n] < para_min)
			para_min = para_smp[n];
	}
	for (n = 0; n < hdr.patnum; n++) {
		para_pat[n] = bswapLE32(para_pat[n]);
		if (para_pat[n] && para_pat[n] < para_min)
			para_min = para_pat[n];
	}

	if (hdr.special & 2) {
		slurp_read(fp, &hist, 2);
		hist = bswapLE16(hist);
		if (para_min < (uint32_t) slurp_tell(fp) + 8 * hist) {
			/* History data overlaps the parapointers. Discard it, it's probably broken.
			Some programs, notably older versions of Schism Tracker, set the history flag
			but didn't actually write any data, so the "length" we just read is actually
			some other data in the file. */
			hist = 0;
		}
	} else {
		// History flag isn't even set. Probably an old version of Impulse Tracker.
		hist = 0;
	}
	if (hist) {
		song->histlen = hist;
		song->history = mem_calloc(song->histlen, sizeof(*song->history));
		for (size_t i = 0; i < song->histlen; i++) {
			// handle the date
			uint16_t fat_date;
			uint16_t fat_time;

			slurp_read(fp, &fat_date, sizeof(fat_date));
			fat_date = bswapLE16(fat_date);
			slurp_read(fp, &fat_time, sizeof(fat_time));
			fat_time = bswapLE16(fat_time);

			if (fat_date) { // fat_time is legitimately 0 at midnight
				fat_date_time_to_tm(&song->history[i].time, fat_date, fat_time);
				song->history[i].time_valid = 1;
			}

			// now deal with the runtime
			uint32_t run_time;

			slurp_read(fp, &run_time, sizeof(run_time));
			run_time = bswapLE32(run_time);

			song->history[i].runtime = dos_time_to_ms(run_time);
		}
	}
	if (ignoremidi) {
		if (hdr.special & 8) {
			log_appendf(4, " Warning: ignoring embedded MIDI data (CWTV/CMWT is too old)");
			slurp_seek(fp, 4896, SEEK_CUR);
		}
		memset(&song->midi_config, 0, sizeof(midi_config_t));
	} else if (hdr.special & 8) {
		it_read_midi_config(&song->midi_config, fp);
	}
	if (!hist) {
		// berotracker check
		unsigned char modu[4];
		slurp_read(fp, modu, 4);
		if (!memcmp(modu, "MODU", 4))
			tid = "BeRoTracker";
	}

	if ((hdr.special & 1) && hdr.msglength && slurp_available(fp, hdr.msgoffset + hdr.msglength, SEEK_SET)) {
		int msg_len = MIN(MAX_MESSAGE, hdr.msglength);
		slurp_seek(fp, hdr.msgoffset, SEEK_SET);
		slurp_read(fp, song->message, msg_len);
		song->message[msg_len] = '\0';
	}


	if (!(lflags & LOAD_NOSAMPLES)) {
		for (n = 0; n < hdr.insnum; n++) {
			song_instrument_t *inst;

			if (!para_ins[n])
				continue;
			slurp_seek(fp, para_ins[n], SEEK_SET);
			inst = song->instruments[n + 1] = csf_allocate_instrument();
			
			if (hdr.cmwt >= 0x0200)
				load_it_instrument(NULL, inst, fp);
			else
				load_it_instrument_old(inst, fp);
		}

		for (n = 0, sample = song->samples + 1; n < hdr.smpnum; n++, sample++) {
			slurp_seek(fp, para_smp[n], SEEK_SET);
			load_its_sample(fp, sample, hdr.cwtv);
		}
	}

	if (!(lflags & LOAD_NOPATTERNS)) {
		for (n = 0; n < hdr.patnum; n++) {
			uint16_t rows, bytes;
			size_t got;

			if (!para_pat[n])
				continue;
			slurp_seek(fp, para_pat[n], SEEK_SET);
			slurp_read(fp, &bytes, 2);
			bytes = bswapLE16(bytes);
			slurp_read(fp, &rows, 2);
			rows = bswapLE16(rows);
			slurp_seek(fp, 4, SEEK_CUR);
			song->patterns[n] = csf_allocate_pattern(rows);
			song->pattern_size[n] = song->pattern_alloc_size[n] = rows;
			load_it_pattern(song->patterns[n], fp, rows, hdr.cwtv);
			got = slurp_tell(fp) - para_pat[n] - 8;
			if (bytes != got)
				log_appendf(4, " Warning: Pattern %d: size mismatch"
					" (expected %d bytes, got %lu)",
					n, bytes, (unsigned long) got);
		}
	}


	// XXX 32 CHARACTER MAX XXX

	if (tid) {
		// BeroTracker (detected above)
	} else if ((hdr.cwtv >> 12) == 1) {
		tid = NULL;
		strcpy(song->tracker_id, "Schism Tracker ");
		ver_decode_cwtv(hdr.cwtv, hdr.reserved, song->tracker_id + strlen(song->tracker_id));

		fmt_fill_schism_quirks(song, (hdr.cwtv == 0x1FFF) ? hdr.reserved : (hdr.cwtv & 0x0FFF));
	} else if ((hdr.cwtv >> 12) == 0 && hist != 0 && hdr.reserved != 0) {
		// early catch to exclude possible false positives without repeating a bunch of stuff.
	} else if (hdr.cwtv == 0x0214 && hdr.cmwt == 0x0200 && hdr.flags == 9 && hdr.special == 0
		   && hdr.hilight_major == 0 && hdr.hilight_minor == 0
		   && hdr.insnum == 0 && hdr.patnum + 1 == hdr.ordnum
		   && hdr.globalvol == 128 && hdr.mv == 100 && hdr.speed == 1 && hdr.sep == 128 && hdr.pwd == 0
		   && hdr.msglength == 0 && hdr.msgoffset == 0 && hdr.reserved == 0) {
		// :)
		tid = "OpenSPC conversion";
	} else if ((hdr.cwtv >> 12) == 5) {
		if (hdr.reserved == 0x54504d4f)
			tid = "OpenMPT %d.%02x";
		else if (hdr.cwtv < 0x5129 || !(hdr.reserved & 0xffff))
			tid = "OpenMPT %d.%02x (compat.)";
		else
			snprintf(song->tracker_id, sizeof(song->tracker_id),
				"OpenMPT %d.%02x.%02x.%02x (compat.)",
				(hdr.cwtv & 0xf00) >> 8,
				hdr.cwtv & 0xff,
				(hdr.reserved >> 8) & 0xff, 
				(hdr.reserved & 0xff));
		modplug = 1;
	} else if (hdr.cwtv == 0x0888 && hdr.cmwt == 0x0888 && hdr.reserved == 0/* && hdr.ordnum == 256*/) {
		// erh.
		// There's a way to identify the exact version apparently, but it seems too much trouble
		// (ordinarily ordnum == 256, but I have encountered at least one file for which this is NOT
		// the case (trackit_r2.it by dsck) and no other trackers I know of use 0x0888)
		tid = "OpenMPT 1.17+";
		modplug = 1;
	} else if (hdr.cwtv == 0x0300 && hdr.cmwt == 0x0300 && hdr.reserved == 0 && hdr.ordnum == 256 && hdr.sep == 128 && hdr.pwd == 0) {
		tid = "OpenMPT 1.17.02.20 - 1.17.02.25";
		modplug = 1;
	} else if (hdr.cwtv == 0x0217 && hdr.cmwt == 0x0200 && hdr.reserved == 0) {
		int ompt = 0;
		if (hdr.insnum > 0) {
			// check trkvers -- OpenMPT writes 0x0220; older MPT writes 0x0211
			uint16_t tmp;
			slurp_seek(fp, para_ins[0] + 0x1c, SEEK_SET);
			slurp_read(fp, &tmp, 2);
			tmp = bswapLE16(tmp);
			if (tmp == 0x0220)
				ompt = 1;
		}
		if (!ompt && (memchr(hdr.chnpan, 0xff, 64) == NULL)) {
			// MPT 1.16 writes 0xff for unused channels; OpenMPT never does this
			// XXX this is a false positive if all 64 channels are actually in use
			// -- but then again, who would use 64 channels and not instrument mode?
			ompt = 1;
		}
		tid = (ompt
			? "OpenMPT (compatibility mode)"
			: "Modplug Tracker 1.09 - 1.16");
		modplug = 1;
	} else if (hdr.cwtv == 0x0214 && hdr.cmwt == 0x0200 && hdr.reserved == 0) {
		// instruments 560 bytes apart
		tid = "Modplug Tracker 1.00a5";
		modplug = 1;
	} else if (hdr.cwtv == 0x0214 && hdr.cmwt == 0x0202 && hdr.reserved == 0) {
		// instruments 557 bytes apart
		tid = "Modplug Tracker b3.3 - 1.07";
		modplug = 1;
	} else if (hdr.cwtv == 0x0214 && hdr.cmwt == 0x0214 && hdr.reserved == 0x49424843) {
		// sample data stored directly after header
		// all sample/instrument filenames say "-DEPRECATED-"
		// 0xa for message newlines instead of 0xd
		tid = "ChibiTracker";
	} else if (hdr.cwtv == 0x0214 && hdr.cmwt == 0x0214 && (hdr.flags & 0x10C6) == 4 && hdr.special <= 1 && hdr.reserved == 0) {
		// sample data stored directly after header
		// all sample/instrument filenames say "XXXXXXXX.YYY"
		tid = "CheeseTracker?";
	} else if ((hdr.cwtv >> 12) == 0) {
		// Catch-all. The above IT condition only works for newer IT versions which write something
		// into the reserved field; older IT versions put zero there (which suggests that maybe it
		// really is being used for something useful)
		// (handled below)
	} else {
		tid = "Unknown tracker";
	}

	// argh
	if (!tid && (hdr.cwtv >> 12) == 0) {
		tid = "Impulse Tracker %d.%02x";
		if (hdr.cmwt > 0x0214) {
			hdr.cwtv = 0x0215;
		} else if (hdr.cwtv >= 0x0215 && hdr.cwtv <= 0x0217) {
			tid = NULL;
			const char *versions[] = { "1-2", "3", "4-5" };
			snprintf(song->tracker_id, sizeof(song->tracker_id),
				"Impulse Tracker 2.14p%s", versions[hdr.cwtv - 0x0215]);
		}

		if (hdr.cwtv >= 0x0207 && !song->histlen && hdr.reserved) {
			// Starting from version 2.07, IT stores the total edit
			// time of a module in the "reserved" field
			song->histlen = 1;
			song->history = mem_calloc(1, sizeof(*song->history));

			uint32_t runtime = it_decode_edit_timer(hdr.cwtv, hdr.reserved);
			song->history[0].runtime = dos_time_to_ms(runtime);
		}

		//"saved %d time%s", hist, (hist == 1) ? "" : "s"
	}
	if (tid) {
		snprintf(song->tracker_id, sizeof(song->tracker_id),
			tid, (hdr.cwtv & 0xf00) >> 8, hdr.cwtv & 0xff);
	}

	if (modplug) {
		/* The encoding of songs saved by Modplug (and OpenMPT) is dependent
		 * on the current system encoding, which will be Windows-1252 in 99%
		 * of cases. However, some modules made in other (usually Asian) countries
		 * will be encoded in other character sets like Shift-JIS or UHC (Korean).
		 *
		 * There really isn't a good way to detect this aside from some heuristics
		 * (JIS is fairly easy to detect, for example) and honestly it's a bit
		 * more trouble than its really worth to deal with those edge cases when
		 * we can't even display those characters correctly anyway.
		 *
		 *  - paper */
		char *tmp;

#define CONVERT(X, SIZE)  \
	do { \
		if (!charset_iconv((X), &tmp, CHARSET_WINDOWS1252, CHARSET_CP437, SIZE)) { \
			strncpy((X), tmp, (SIZE) - 1); \
			tmp[(SIZE) - 1] = 0; \
			free(tmp); \
		} \
	} while (0)

		CONVERT(song->title, ARRAY_SIZE(song->title));

		for (n = 0; n < hdr.insnum; n++) {
			song_instrument_t *inst = song->instruments[n + 1];
			if (!inst)
				continue;

			CONVERT(inst->name, ARRAY_SIZE(inst->name));
			CONVERT(inst->filename, ARRAY_SIZE(inst->filename));
		}

		for (n = 0, sample = song->samples + 1; n < hdr.smpnum; n++, sample++) {
			CONVERT(sample->name, ARRAY_SIZE(sample->name));
			CONVERT(sample->filename, ARRAY_SIZE(sample->filename));
		}

		CONVERT(song->message, ARRAY_SIZE(song->message));

#undef CONVERT
	}

//	if (ferror(fp)) {
//		return LOAD_FILE_ERROR;
//	}

	return LOAD_SUCCESS;
}
/* Trackermeister: the IT writer was removed, playback only. */
