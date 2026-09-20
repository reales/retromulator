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
#include "../include/slurp.h"
#include "../include/fmt.h"
#include "../include/version.h"
#include "../include/mem.h"

#include "../include/player/sndfile.h"

#include "../include/disko.h"
#include "../include/log.h"

/* --------------------------------------------------------------------- */

int fmt_s3m_read_info(dmoz_file_t *file, slurp_t *fp)
{
	unsigned char magic[4], title[27];
	
	slurp_seek(fp, 44, SEEK_SET);
	if (slurp_read(fp, magic, sizeof(magic)) != sizeof(magic)
		|| memcmp(magic, "SCRM", sizeof(magic)))
		return 0;

	slurp_rewind(fp);
	if (slurp_read(fp, title, sizeof(title)) != sizeof(title))
		return 0;

	file->description = "Scream Tracker 3";
	/*file->extension = str_dup("s3m");*/
	file->title = strn_dup((const char *)title, sizeof(title));
	file->type = TYPE_MODULE_S3M;
	return 1;
}

/* --------------------------------------------------------------------------------------------------------- */

/* converts 0..15 value to IT-like 0..64 value.
 *
 * 0 -> 2
 * 1 -> 6
 * ...
 * 14 -> 58
 * 15 -> 62 */
static int s3m_pan_to_it(int p)
{
	return ((p & 15) * 4) + 2;
}

/* converts 0..64 value to S3M-like 0..15 value.
 *
 * basically the inverse of the above function
 * with some other things too */
static int it_pan_to_s3m(int p)
{
    /* this is necessary so we don't do out of range */
    p = MAX(p, 2);

    /* We don't subtract two here, like we do in the
     * s3m pan to it function. In fact this is simply
     * a clever trick to get round-to-nearest for free.
     * If we were to subtract two and wanted to round,
     * it would effectively be a no-op. */
    p /= 4;

    p = MIN(p, 15);

    return p;
}

/* IMPORTANT NOTE:
 * Whenever changing the above two helper functions, be ABSOLUTELY sure
 * that they are actually inverses of each other.
 * You can verify this by plopping them into their own C file and running
 * this program:

int main(void)
{
    int i;

    for (i = 0; i <= 15; i++) {
        assert(it_pan_to_s3m(s3m_pan_to_it(i)) == i);
    }

    return 0;
}

 * Of course, converting IT to S3M panning is usually a lossy process
 * anyway, but we don't want values to randomly shift when importing
 * and saving modules.
*/

enum {
	S3I_TYPE_NONE = 0,
	S3I_TYPE_PCM = 1,
	S3I_TYPE_ADMEL = 2,
	S3I_TYPE_CONTROL = 0xff, // only internally used for saving
};


/* misc flags for loader (internal) */
#define S3M_UNSIGNED 1
#define S3M_CHANPAN 2 // the FC byte

static int s3m_import_edittime(song_t *song, uint16_t trkvers, uint32_t reserved32)
{
	if (song->histlen)
		return 0; // ?

	song->histlen = 1;
	song->history = mem_calloc(1, sizeof(*song->history));

	uint32_t runtime = it_decode_edit_timer(trkvers, reserved32);
	song->history[0].runtime = dos_time_to_ms(runtime);

	return 1;
}

int fmt_s3m_load_song(song_t *song, slurp_t *fp, uint32_t lflags)
{
	uint16_t nsmp, nord, npat;
	int misc = S3M_UNSIGNED | S3M_CHANPAN; // temporary flags, these are both generally true
	int n;
	song_note_t *note;
	/* junk variables for reading stuff into */
	uint16_t tmp;
	uint8_t c;
	uint32_t tmplong;
	uint8_t b[4];
	uint8_t channel_types[32];
	/* parapointers */
	uint16_t para_smp[MAX_SAMPLES];
	uint16_t para_pat[MAX_PATTERNS];
	uint32_t para_sdata[MAX_SAMPLES] = { 0 };
	uint32_t smp_flags[MAX_SAMPLES] = { 0 };
	song_sample_t *sample;
	uint16_t trkvers;
	uint16_t flags;
	uint16_t special;
	uint8_t reserved[8];
	uint16_t reserved16low; // low 16 bits of version info
	uint16_t reserved16high; // high 16 bits of version info
	uint32_t reserved32; // Impulse Tracker edit time
	uint32_t adlib = 0; // bitset
	uint16_t gus_addresses = 0;
	uint8_t mix_volume; /* detect very old modplug tracker */
	char any_samples = 0;
	int uc;
	const char *tid = NULL;

	/* check the tag */
	slurp_seek(fp, 44, SEEK_SET);
	slurp_read(fp, b, 4);
	if (memcmp(b, "SCRM", 4) != 0)
		return LOAD_UNSUPPORTED;

	/* read the title */
	slurp_rewind(fp);
	slurp_read(fp, song->title, 25);
	song->title[25] = 0;

	/* skip the last three bytes of the title, the supposed-to-be-0x1a byte,
	the tracker ID, and the two useless reserved bytes */
	slurp_seek(fp, 7, SEEK_CUR);

	slurp_read(fp, &nord, 2);
	slurp_read(fp, &nsmp, 2);
	slurp_read(fp, &npat, 2);
	nord = bswapLE16(nord);
	nsmp = bswapLE16(nsmp);
	npat = bswapLE16(npat);

	if (nord > MAX_ORDERS || nsmp > MAX_SAMPLES || npat > MAX_PATTERNS)
		return LOAD_FORMAT_ERROR;

	song->flags = SONG_ITOLDEFFECTS;
	slurp_read(fp, &flags, 2);  /* flags (don't really care) */
	flags = bswapLE16(flags);
	slurp_read(fp, &trkvers, 2);
	trkvers = bswapLE16(trkvers);
	slurp_read(fp, &tmp, 2);  /* file format info */
	if (tmp == bswapLE16(1))
		misc &= ~S3M_UNSIGNED;     /* signed samples (ancient s3m) */

	slurp_seek(fp, 4, SEEK_CUR); /* skip the tag */

	song->initial_global_volume = slurp_getc(fp) << 1;
	// In the case of invalid data, ST3 uses the speed/tempo value that's set in the player prior to
	// loading the song, but that's just crazy.
	song->initial_speed = slurp_getc(fp);
	if (!song->initial_speed)
		song->initial_speed = 6;

	song->initial_tempo = slurp_getc(fp);
	if (song->initial_tempo <= 32) {
		// (Yes, 32 is ignored by Scream Tracker.)
		song->initial_tempo = 125;
	}
	mix_volume = song->mixing_volume = slurp_getc(fp);
	if (song->mixing_volume & 0x80) {
		song->mixing_volume ^= 0x80;
	} else {
		song->flags |= SONG_NOSTEREO;
	}
	uc = slurp_getc(fp); /* ultraclick removal (useless) */

	if (slurp_getc(fp) != 0xfc)
		misc &= ~S3M_CHANPAN;     /* stored pan values */

	slurp_read(fp, &reserved, 8);
	memcpy(&reserved16low, reserved, 2);
	memcpy(&reserved32, reserved + 2, 4);
	memcpy(&reserved16high, reserved + 6, 2);
	reserved16low = bswapLE16(reserved16low); // schism & openmpt version info
	reserved32 = bswapLE32(reserved32); // impulse tracker edit timer
	reserved16high = bswapLE16(reserved16high); // high bits of schism version info
	slurp_read(fp, &special, 2); // field not used by st3
	special = bswapLE16(special);

	/* channel settings */
	slurp_read(fp, channel_types, 32);
	for (n = 0; n < 32; n++) {
		/* Channel 'type': 0xFF is a disabled channel, which shows up as (--) in ST3.
		Any channel with the high bit set is muted.
		00-07 are L1-L8, 08-0F are R1-R8, 10-18 are adlib channels A1-A9.
		Hacking at a file with a hex editor shows some perhaps partially-implemented stuff:
		types 19-1D show up in ST3 as AB, AS, AT, AC, and AH; 20-2D are the same as 10-1D
		except with 'B' insted of 'A'. None of these appear to produce any sound output,
		apart from 19 which plays adlib instruments briefly before cutting them. (Weird!)
		Also, 1E/1F and 2E/2F display as "??"; and pressing 'A' on a disabled (--) channel
		will change its type to 1F.
		Values past 2F seem to display bits of the UI like the copyright and help, strange!
		These out-of-range channel types will almost certainly hang or crash ST3 or
		produce other strange behavior. Simply put, don't do it. :) */
		c = channel_types[n];
		if (c & 0x80) {
			song->channels[n].flags |= CHN_MUTE;
			// ST3 doesn't even play effects in muted channels -- throw them out?
			c &= ~0x80;
		}
		if (c < 0x08) {
			// L1-L8 (panned to 3 in ST3)
			song->channels[n].panning = 14;
		} else if (c < 0x10) {
			// R1-R8 (panned to C in ST3)
			song->channels[n].panning = 50;
		} else if (c < 0x19) {
			// A1-A9
			song->channels[n].panning = 32;
			adlib |= 1 << n;
		} else {
			// Disabled 0xff/0x7f, or broken
			song->channels[n].panning = 32;
			song->channels[n].flags |= CHN_MUTE;
		}
		song->channels[n].volume = 64;
	}
	for (; n < MAX_CHANNELS; n++) {
		song->channels[n].panning = 32;
		song->channels[n].volume = 64;
		song->channels[n].flags = CHN_MUTE;
	}

	// Schism Tracker before 2018-11-12 played AdLib instruments louder than ST3. Compensate by lowering the sample mixing volume.
	if (adlib && trkvers >= 0x4000 && trkvers < 0x4D33) {
		song->mixing_volume = song->mixing_volume * 2274 / 4096;
	}

	/* orderlist */
	slurp_read(fp, song->orderlist, nord);
	memset(song->orderlist + nord, ORDER_LAST, MAX_ORDERS - nord);

	/* load the parapointers */
	slurp_read(fp, para_smp, 2 * nsmp);
	slurp_read(fp, para_pat, 2 * npat);

	/* default pannings */
	if (misc & S3M_CHANPAN) {
		for (n = 0; n < 32; n++) {
			int pan = slurp_getc(fp);
			if ((pan & 0x20) && (!(adlib & (1 << n)) || trkvers > 0x1320))
				song->channels[n].panning = s3m_pan_to_it(pan);
		}
	}

	//mphack - fix the pannings
	for (n = 0; n < MAX_CHANNELS; n++)
		song->channels[n].panning *= 4;

	/* samples */
	for (n = 0, sample = song->samples + 1; n < nsmp; n++, sample++) {
		uint8_t type;

		slurp_seek(fp, bswapLE16(para_smp[n]) << 4, SEEK_SET);

		type = slurp_getc(fp);
		slurp_read(fp, sample->filename, 12);
		sample->filename[12] = 0;

		slurp_read(fp, b, 3); // data pointer for pcm, irrelevant otherwise
		switch (type) {
		case S3I_TYPE_PCM:
			para_sdata[n] = b[1] | (b[2] << 8) | (b[0] << 16);
			slurp_read(fp, &tmplong, 4);
			sample->length = bswapLE32(tmplong);
			slurp_read(fp, &tmplong, 4);
			sample->loop_start = bswapLE32(tmplong);
			slurp_read(fp, &tmplong, 4);
			sample->loop_end = bswapLE32(tmplong);
			sample->volume = slurp_getc(fp) * 4; //mphack
			slurp_getc(fp);      /* unused byte */
			slurp_getc(fp);      /* packing info (never used) */
			c = slurp_getc(fp);  /* flags */
			if (c & 1)
				sample->flags |= CHN_LOOP;
			smp_flags[n] = (SF_LE
				| ((misc & S3M_UNSIGNED) ? SF_PCMU : SF_PCMS)
				| ((c & 4) ? SF_16 : SF_8)
				| ((c & 2) ? SF_SS : SF_M));
			if (sample->length)
				any_samples = 1;
			break;

		default:
			//printf("s3m: mystery-meat sample type %d\n", type);
		case S3I_TYPE_NONE:
			slurp_seek(fp, 12, SEEK_CUR);
			sample->volume = slurp_getc(fp) * 4; //mphack
			slurp_seek(fp, 3, SEEK_CUR);
			break;

		case S3I_TYPE_ADMEL:
			slurp_read(fp, sample->adlib_bytes, 12);
			sample->volume = slurp_getc(fp) * 4; //mphack
			// next byte is "dsk", what is that?
			slurp_seek(fp, 3, SEEK_CUR);
			sample->flags |= CHN_ADLIB;
			// dumb hackaround that ought to some day be fixed:
			sample->length = 1;
			sample->data = csf_allocate_sample(1);
			break;
		}

		slurp_read(fp, &tmplong, 4);
		sample->c5speed = bswapLE32(tmplong);
		if (type == S3I_TYPE_ADMEL) {
			if (sample->c5speed < 1000 || sample->c5speed > 0xFFFF) {
				sample->c5speed = 8363;
			}
		}
		slurp_seek(fp, 4, SEEK_CUR);        /* unused space */
		int16_t gus_address;
		slurp_read(fp, &gus_address, 2);
		gus_addresses |= bswapLE16(gus_address);
		slurp_seek(fp, 6, SEEK_CUR);
		slurp_read(fp, sample->name, 25);
		sample->name[25] = 0;
		sample->vib_type = 0;
		sample->vib_rate = 0;
		sample->vib_depth = 0;
		sample->vib_speed = 0;
		sample->global_volume = 64;
	}

	/* sample data */
	if (!(lflags & LOAD_NOSAMPLES)) {
		for (n = 0, sample = song->samples + 1; n < nsmp; n++, sample++) {
			if (!sample->length || (sample->flags & CHN_ADLIB))
				continue;
			slurp_seek(fp, para_sdata[n] << 4, SEEK_SET);
			csf_read_sample(sample, smp_flags[n], fp);
		}
	}

	// Mixing volume is not used with the GUS driver; relevant for PCM + OPL tracks
	if (gus_addresses > 1)
		song->mixing_volume = 48;

	if (!(lflags & LOAD_NOPATTERNS)) {
		for (n = 0; n < npat; n++) {
			int row = 0;
			long end;

			para_pat[n] = bswapLE16(para_pat[n]);
			if (!para_pat[n])
				continue;

			slurp_seek(fp, para_pat[n] << 4, SEEK_SET);
			slurp_read(fp, &tmp, 2);
			end = (para_pat[n] << 4) + bswapLE16(tmp) + 2;

			song->patterns[n] = csf_allocate_pattern(64);

			while (row < 64 && slurp_tell(fp) < end) {
				int mask = slurp_getc(fp);
				uint8_t chn = (mask & 31);

				if (mask == EOF) {
					log_appendf(4, " Warning: Pattern %d: file truncated", n);
					break;
				}
				if (!mask) {
					/* done with the row */
					row++;
					continue;
				}
				note = song->patterns[n] + MAX_CHANNELS * row + chn;
				if (mask & 32) {
					/* note/instrument */
					note->note = slurp_getc(fp);
					note->instrument = slurp_getc(fp);
					//if (note->instrument > 99)
					//      note->instrument = 0;
					switch (note->note) {
					default:
						// Note; hi=oct, lo=note
						note->note = (note->note >> 4) * 12 + (note->note & 0xf) + 13;
						break;
					case 255:
						note->note = NOTE_NONE;
						break;
					case 254:
						note->note = (adlib & (1 << chn)) ? NOTE_OFF : NOTE_CUT;
						break;
					}
				}
				if (mask & 64) {
					/* volume */
					note->voleffect = VOLFX_VOLUME;
					note->volparam = slurp_getc(fp);
					if (note->volparam == 255) {
						note->voleffect = VOLFX_NONE;
						note->volparam = 0;
					} else if (note->volparam >= 128 && note->volparam <= 192) {
						// ModPlug (or was there any earlier tracker using this command?)
						note->voleffect = VOLFX_PANNING;
						note->volparam -= 128;
					} else if (note->volparam > 64) {
						// some weirdly saved s3m?
						note->volparam = 64;
					}
				}
				if (mask & 128) {
					note->effect = slurp_getc(fp);
					note->param = slurp_getc(fp);
					csf_import_s3m_effect(note, 0);
					if (note->effect == FX_SPECIAL) {
						// mimic ST3's SD0/SC0 behavior
						if (note->param == 0xd0) {
							note->note = NOTE_NONE;
							note->instrument = 0;
							note->voleffect = VOLFX_NONE;
							note->volparam = 0;
							note->effect = FX_NONE;
							note->param = 0;
						} else if (note->param == 0xc0) {
							note->effect = FX_NONE;
							note->param = 0;
						} else if ((note->param & 0xf0) == 0xa0) {
							// Convert the old messy SoundBlaster stereo control command (or an approximation of it, anyway)
							uint8_t ctype = channel_types[chn] & 0x7f;
							if (gus_addresses > 1 || ctype >= 0x10)
								note->effect = FX_NONE;
							else if (note->param == 0xa0 || note->param == 0xa2)  // Normal panning
								note->param = (ctype & 8) ? 0x8c : 0x83;
							else if (note->param == 0xa1 || note->param == 0xa3)  // Swap left / right channel
								note->param = (ctype & 8) ? 0x83 : 0x8c;
							else if (note->param <= 0xa7)  // Center
								note->param = 0x88;
							else
								note->effect = FX_NONE;
						}
					}
				}
				/* ... next note, same row */
			}
		}
	}

	/* MPT identifies as ST3.20 in the trkvers field, but it puts zeroes for the 'special' field, only ever
	 * sets flags 0x10 and 0x40, writes multiples of 16 orders, always saves channel pannings, and writes
	 * zero into the ultraclick removal field. (ST3.2x always puts either 16, 24, or 32 there, older versions put 0).
	 * Velvet Studio also pretends to be ST3, but writes zeroes for 'special'. ultraclick, and flags, and
	 * does NOT save channel pannings. Also, it writes a fairly recognizable LRRL pattern for the channels,
	 * but I'm not checking that. (yet?) */
	if (trkvers == 0x1320) {
		if (!memcmp(reserved, "SCLUB2.0", 8)) {
			tid = "Sound Club 2";
		} else if (special == 0 && uc == 0 && (flags & ~0x50) == 0
		    && misc == (S3M_UNSIGNED | S3M_CHANPAN) && (nord % 16) == 0) {
			/* from OpenMPT:
			 * MPT 1.0 alpha5 doesn't set the stereo flag, but MPT 1.0 alpha6 does. */

			tid = ((mix_volume & 0x80) != 0)
				? "ModPlug Tracker / OpenMPT 1.17"
				: "ModPlug Tracker 1.0 alpha";
		} else if (special == 0 && uc == 0 && flags == 0 && misc == S3M_UNSIGNED) {
			if (song->initial_global_volume == 128 && mix_volume == 48)
				tid = "PlayerPRO";
			else  // Always stereo
				tid = "Velvet Studio";
		} else if(special == 0 && uc == 0 && flags == 8 && misc == S3M_UNSIGNED) {
			tid = "Impulse Tracker < 1.03";  // Not sure if 1.02 saves like this as I don't have it
		} else if (uc != 16 && uc != 24 && uc != 32) {
			// sure isn't scream tracker
			tid = "Unknown tracker";
		}
	}

	if (!tid) {
		switch (trkvers >> 12) {
		case 0:
			if (trkvers == 0x0208)
				strcpy(song->tracker_id, "Akord");
			break;
		case 1:
			if (gus_addresses > 1)
				tid = "Scream Tracker %" PRIu8 ".%02" PRIx8 " (GUS)";
			else if (gus_addresses == 1 || !any_samples || trkvers == 0x1300)
				tid = "Scream Tracker %" PRIu8 ".%02" PRIx8 " (SB)"; // could also be a GUS file with a single sample
			else {
				strcpy(song->tracker_id, "Unknown tracker");
				if (trkvers == 0x1301 && uc == 0) {
					if (!(flags & ~0x50) && (mix_volume & 0x80) && (misc & S3M_CHANPAN))
						strcpy(song->tracker_id, "UNMO3");
					else if (!flags && song->initial_global_volume == 96 && mix_volume == 176 && song->initial_tempo == 150 && !(misc & S3M_CHANPAN))
						strcpy(song->tracker_id, "deMODifier");  // SoundSmith to S3M converter
					else if (!flags && song->initial_global_volume == 128 && song->initial_speed == 6 && song->initial_tempo == 125 && !(misc & S3M_CHANPAN))
						strcpy(song->tracker_id, "Kosmic To-S3M");  // MTM to S3M converter by Zab/Kosmic
				}
			}
			break;
		case 2:
			if (trkvers == 0x2013) // PlayerPRO on Intel forgets to byte-swap the tracker ID bytes 
				strcpy(song->tracker_id, "PlayerPRO");
			else
				tid = "Imago Orpheus %" PRIu8 ".%02" PRIx8;
			break;
		case 3:
			/* TODO this stuff is duped in fmt/it.c, move it elsewhere ? */
			if (trkvers <= 0x3214) {
				tid = "Impulse Tracker %" PRIu8 ".%02" PRIx8;
			} else if (trkvers == 0x3320) {
				tid = "Impulse Tracker 1.03";  // Could also be 1.02, maybe? I don't have that one
			} else if(trkvers >= 0x3215 && trkvers <= 0x3217) {
				tid = NULL;
				const char *versions[] = { "1-2", "3", "4-5" };
				snprintf(song->tracker_id, sizeof(song->tracker_id),
					"Impulse Tracker 2.14p%s", versions[trkvers - 0x3215]);
			}

			if (trkvers >= 0x3207 && trkvers <= 0x3217 && reserved32)
				s3m_import_edittime(song, trkvers, reserved32);

			break;
		case 4:
			if (trkvers == 0x4100) {
				strcpy(song->tracker_id, "BeRoTracker");
			} else {
				uint32_t full_version = (((uint32_t)reserved16high) << 16) | (reserved16low);
				strcpy(song->tracker_id, "Schism Tracker ");
				ver_decode_cwtv(trkvers, full_version, song->tracker_id + strlen(song->tracker_id));
				if (trkvers == 0x4fff && full_version >= ver_mktime(2024, 11, 24))
					s3m_import_edittime(song, 0x0000, reserved32);
			}
			break;
		case 5:
			/* from OpenMPT src:
			 *
			 * Liquid Tracker's ID clashes with OpenMPT's.
			 * OpenMPT started writing full version information with OpenMPT 1.29 and later changed the ultraClicks value from 8 to 16.
			 * Liquid Tracker writes an ultraClicks value of 16.
			 * So we assume that a file was saved with Liquid Tracker if the reserved fields are 0 and ultraClicks is 16. */
			if ((trkvers >> 8) == 0x57) {
				tid = "NESMusa %" PRIu8 ".%" PRIX8; /* tool by Bisquit */
			} else if (!reserved16low && uc == 16 && channel_types[1] != 1) {
				tid = "Liquid Tracker %" PRIu8 ".%" PRIX8;
			} else if (trkvers == 0x5447) {
				strcpy(song->tracker_id, "Graoumf Tracker");
			} else if (trkvers >= 0x5129 && reserved16low) {
				/* e.x. 1.29.01.12 <-> 0x01290112 */
				const uint32_t ver = (((trkvers & 0xfff) << 16) | reserved16low);
				snprintf(song->tracker_id, sizeof(song->tracker_id),
					"OpenMPT %" PRIu32 ".%02" PRIX32 ".%02" PRIX32 ".%02" PRIX32,
					ver >> 24,
					(ver >> 16) & 0xFF,
					(ver >> 8) & 0xFF,
					(ver) & 0xFF);
				if (ver >= UINT32_C(0x01320031))
					s3m_import_edittime(song, 0x0000, reserved32);
			} else {
				tid = "OpenMPT %" PRIu8 ".%02" PRIX8;
			}
			break;
		case 6:
			strcpy(song->tracker_id, "BeRoTracker");
			break;
		case 7:
			strcpy(song->tracker_id, "CreamTracker");
			break;
		case 12:
			if (trkvers == 0xCA00)
				strcpy(song->tracker_id, "Camoto");
			break;
		default:
			break;
		}
	}
	if (tid)
		snprintf(song->tracker_id, sizeof(song->tracker_id),
			tid, (uint8_t)((trkvers & 0xf00) >> 8),
			(uint8_t)(trkvers & 0xff));

//      if (ferror(fp)) {
//              return LOAD_FILE_ERROR;
//      }
	/* done! */
	return LOAD_SUCCESS;
}

/* Trackermeister: the S3M writer was removed, playback only. */
