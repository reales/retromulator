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
#include "../include/fmt.h"

#include "../include/it.h"
#include "../include/song.h"
#include "../include/log.h"
#include "../include/version.h"
#include "../include/mem.h"

/* -------------------------------------------------------- */

struct it_envelope {
	uint8_t flags;
	uint8_t num;
	uint8_t lpb;
	uint8_t lpe;
	uint8_t slb;
	uint8_t sle;
	struct {
		int8_t value; // signed (-32 -> 32 for pan and pitch; 0 -> 64 for vol and filter)
		uint16_t tick;
	} nodes[25];
	uint8_t reserved;
};

// Old Impulse Instrument Format (cmwt < 0x200)
struct it_instrument_old {
	uint32_t id;                    // IMPI = 0x49504D49
	int8_t filename[12];    // DOS file name
	uint8_t zero;
	uint8_t flags;
	uint8_t vls;
	uint8_t vle;
	uint8_t sls;
	uint8_t sle;
	uint16_t reserved1;
	uint16_t fadeout;
	uint8_t nna;
	uint8_t dnc;
	uint16_t trkvers;
	uint8_t nos;
	uint8_t reserved2;
	int8_t name[26];
	uint16_t reserved3[3];
	//struct it_notetrans keyboard[120];
	uint8_t volenv[200];
	uint8_t nodes[50];
};

// Impulse Instrument Format
struct it_instrument {
	uint32_t id;
	int8_t filename[12];
	uint8_t zero;
	uint8_t nna;
	uint8_t dct;
	uint8_t dca;
	uint16_t fadeout;
	signed char pps;
	uint8_t ppc;
	uint8_t gbv;
	uint8_t dfp;
	uint8_t rv;
	uint8_t rp;
	uint16_t trkvers;
	uint8_t nos;
	uint8_t reserved1;
	int8_t name[26];
	uint8_t ifc;
	uint8_t ifr;
	uint8_t mch;
	uint8_t mpr;
	uint16_t mbank;
	//struct it_notetrans keyboard[120];
	struct it_envelope volenv;
	struct it_envelope panenv;
	struct it_envelope pitchenv;
	uint8_t dummy[4]; // was 7, but IT v2.17 saves 554 bytes
};

/* --------------------------------------------------------------------- */

static int load_it_notetrans(struct instrumentloader *ii, song_instrument_t *instrument, slurp_t *fp,
	int *numsamps)
{
	/* sample bits; made an array simply because it means it's expandible */
	BITARRAY_DECLARE(smpbits, 256);
	int n;

	BITARRAY_ZERO(smpbits);

	for (n = 0; n < 120; n++) {
		int note = slurp_getc(fp);
		int smp = slurp_getc(fp);
		if (note == EOF || smp == EOF)
			return 0;

		note += NOTE_FIRST;
		// map invalid notes to themselves
		if (!NOTE_IS_NOTE(note))
			note = n + NOTE_FIRST;

		if (instrument) {
			instrument->note_map[n] = note;
			instrument->sample_map[n] = (ii ? instrument_loader_sample(ii, smp) : smp);
		}

		/* smp == 0 means theres no sample mapped */
		if (!smp)
			continue;

		/* this should never ever happen (UCHAR_MAX == 255) */
		SCHISM_RUNTIME_ASSERT(smp < 256, "overflow");

		BITARRAY_SET(smpbits, smp);
	}

	if (numsamps) {
		*numsamps = 0;

		for (n = 0; n < 256; n++)
			if (BITARRAY_ISSET(smpbits, n))
				(*numsamps)++;
	}

	return 1;
}

static const uint32_t env_flags[3][4] = {
	{ENV_VOLUME,  ENV_VOLLOOP,   ENV_VOLSUSTAIN,   ENV_VOLCARRY},
	{ENV_PANNING, ENV_PANLOOP,   ENV_PANSUSTAIN,   ENV_PANCARRY},
	{ENV_PITCH,   ENV_PITCHLOOP, ENV_PITCHSUSTAIN, ENV_PITCHCARRY},
};

// XXX need to check slurp return values
static uint32_t load_it_envelope(song_envelope_t *env, slurp_t *fp, int envtype, int adj)
{
	struct it_envelope itenv;

	uint32_t flags = 0;
	int n;

	slurp_read(fp, &itenv.flags, sizeof(itenv.flags));
	slurp_read(fp, &itenv.num, sizeof(itenv.num));
	slurp_read(fp, &itenv.lpb, sizeof(itenv.lpb));
	slurp_read(fp, &itenv.lpe, sizeof(itenv.lpe));
	slurp_read(fp, &itenv.slb, sizeof(itenv.slb));
	slurp_read(fp, &itenv.sle, sizeof(itenv.sle));
	for (size_t i = 0; i < ARRAY_SIZE(itenv.nodes); i++) {
		slurp_read(fp, &itenv.nodes[i].value, sizeof(itenv.nodes[i].value));
		slurp_read(fp, &itenv.nodes[i].tick, sizeof(itenv.nodes[i].tick));
	}
	slurp_read(fp, &itenv.reserved, sizeof(itenv.reserved));

	env->nodes = CLAMP(itenv.num, 2, 25);
	env->loop_start = MIN(itenv.lpb, env->nodes);
	env->loop_end = CLAMP(itenv.lpe, env->loop_start, env->nodes);
	env->sustain_start = MIN(itenv.slb, env->nodes);
	env->sustain_end = CLAMP(itenv.sle, env->sustain_start, env->nodes);

	for (n = 0; n < env->nodes; n++) {
		int v = itenv.nodes[n].value + adj;
		env->values[n] = CLAMP(v, 0, 64);
		env->ticks[n] = bswapLE16(itenv.nodes[n].tick);
	}

	env->ticks[0] = 0; // sanity check

	for (n = 0; n < 4; n++) {
		if (itenv.flags & (1 << n))
			flags |= env_flags[envtype][n];
	}

	if (envtype == 2 && (itenv.flags & 0x80))
		flags |= ENV_FILTER;

	return flags;
}

int load_it_instrument_old(song_instrument_t *instrument, slurp_t *fp)
{
	struct it_instrument_old ihdr;
	int n;

#define READ_VALUE(name) do { if (slurp_read(fp, &ihdr.name, sizeof(ihdr.name)) != sizeof(ihdr.name)) { return 0; } } while (0)

	READ_VALUE(id);
	READ_VALUE(filename);
	READ_VALUE(zero);
	READ_VALUE(flags);
	READ_VALUE(vls);
	READ_VALUE(vle);
	READ_VALUE(sls);
	READ_VALUE(sle);
	READ_VALUE(reserved1);
	READ_VALUE(fadeout);
	READ_VALUE(nna);
	READ_VALUE(dnc);
	READ_VALUE(trkvers);
	READ_VALUE(nos);
	READ_VALUE(reserved2);
	READ_VALUE(name);
	READ_VALUE(reserved3);

	/* err */
	if (ihdr.id != bswapLE32(0x49504D49))
		return 0;

	memcpy(instrument->name, ihdr.name, 25);
	instrument->name[25] = '\0';
	memcpy(instrument->filename, ihdr.filename, 12);
	instrument->filename[12] = '\0';

	instrument->nna = ihdr.nna % 4;
	if (ihdr.dnc) {
		// XXX is this right?
		instrument->dct = DCT_NOTE;
		instrument->dca = DCA_NOTECUT;
	}

	instrument->fadeout = bswapLE16(ihdr.fadeout) << 6;
	instrument->pitch_pan_separation = 0;
	instrument->pitch_pan_center = NOTE_MIDC;
	instrument->global_volume = 128;
	instrument->panning = 32 * 4; //mphack

	if (!load_it_notetrans(NULL, instrument, fp, NULL))
		return 0;

	if (ihdr.flags & 1)
		instrument->flags |= ENV_VOLUME;
	if (ihdr.flags & 2)
		instrument->flags |= ENV_VOLLOOP;
	if (ihdr.flags & 4)
		instrument->flags |= ENV_VOLSUSTAIN;

	instrument->vol_env.loop_start = ihdr.vls;
	instrument->vol_env.loop_end = ihdr.vle;
	instrument->vol_env.sustain_start = ihdr.sls;
	instrument->vol_env.sustain_end = ihdr.sle;
	instrument->vol_env.nodes = 25;

	READ_VALUE(volenv);
	READ_VALUE(nodes);

	// this seems totally wrong... why isn't this using ihdr.vol_env at all?
	// apparently it works, though.
	for (n = 0; n < 25; n++) {
		int node = ihdr.nodes[2 * n];
		if (node == 0xff) {
			instrument->vol_env.nodes = n;
			break;
		}
		instrument->vol_env.ticks[n] = node;
		instrument->vol_env.values[n] = ihdr.nodes[2 * n + 1];
	}

#undef READ_VALUE

	return 1;
}

int load_it_instrument(struct instrumentloader* ii, song_instrument_t *instrument, slurp_t *fp)
{
	struct it_instrument ihdr;

#define READ_VALUE(name) do { if (slurp_read(fp, &ihdr.name, sizeof(ihdr.name)) != sizeof(ihdr.name)) { return 0; } } while (0)

	READ_VALUE(id);
	READ_VALUE(filename);
	READ_VALUE(zero);
	READ_VALUE(nna);
	READ_VALUE(dct);
	READ_VALUE(dca);
	READ_VALUE(fadeout);
	READ_VALUE(pps);
	READ_VALUE(ppc);
	READ_VALUE(gbv);
	READ_VALUE(dfp);
	READ_VALUE(rv);
	READ_VALUE(rp);
	READ_VALUE(trkvers);
	READ_VALUE(nos);
	READ_VALUE(reserved1);
	READ_VALUE(name);
	READ_VALUE(ifc);
	READ_VALUE(ifr);
	READ_VALUE(mch);
	READ_VALUE(mpr);
	READ_VALUE(mbank);

#undef READ_VALUE

	/* err */
	if (ihdr.id != bswapLE32(0x49504D49))
		return 0;

	memcpy(instrument->name, ihdr.name, 25);
	instrument->name[25] = '\0';
	memcpy(instrument->filename, ihdr.filename, 12);
	instrument->filename[12] = '\0';

	instrument->nna = ihdr.nna % 4;
	instrument->dct = ihdr.dct % 4;
	instrument->dca = ihdr.dca % 3;
	instrument->fadeout = bswapLE16(ihdr.fadeout) << 5;
	instrument->pitch_pan_separation = CLAMP(ihdr.pps, -32, 32);
	instrument->pitch_pan_center = MIN(ihdr.ppc, 119); // I guess
	instrument->global_volume = MIN(ihdr.gbv, 128);
	instrument->panning = MIN((ihdr.dfp & 127), 64) * 4; //mphack
	if (!(ihdr.dfp & 128))
		instrument->flags |= ENV_SETPANNING;
	instrument->vol_swing = MIN(ihdr.rv, 100);
	instrument->pan_swing = MIN(ihdr.rp, 64);

	instrument->ifc = ihdr.ifc;
	instrument->ifr = ihdr.ifr;

	// (blah... this isn't supposed to be a mask according to the
	// spec. where did this code come from? and what is 0x10000?)
	instrument->midi_channel_mask =
			((ihdr.mch > 16)
			 ? (0x10000 + ihdr.mch)
			 : ((ihdr.mch > 0)
			    ? (1 << (ihdr.mch - 1))
			    : 0));
	instrument->midi_program = ihdr.mpr;
	instrument->midi_bank = bswapLE16(ihdr.mbank);

	if (!load_it_notetrans(ii, instrument, fp, NULL))
		return 0;

	instrument->flags |= load_it_envelope(&instrument->vol_env, fp, 0, 0);
	instrument->flags |= load_it_envelope(&instrument->pan_env, fp, 1, 32);
	instrument->flags |= load_it_envelope(&instrument->pitch_env, fp, 2, 32);

	slurp_seek(fp, 4, SEEK_CUR);

	return 1;
}

/* Trackermeister: the ITI file loader and writer were removed, playback only. */
