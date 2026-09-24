// Trackermeister: headless session around the FT2 replayer (after cli/main.c).

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include "ft2_header.h"
#include "ft2_replayer.h"
#include "ft2_audio.h"
#include "ft2_config.h"
#include "ft2_module_loader.h"
#include "ft2_sample_ed.h"
#include "ft2_inst_ed.h"
#include "mixer/ft2_mix.h"
#include "mixer/ft2_mix_interpolation.h"
#include "tm_ft2.h"

extern bool loadXM(FILE *f, uint32_t filesize);
extern bool loadMOD(FILE *f, uint32_t filesize);

// ft2_replayer.c
extern bool tmAmigaPan;

// ft2_stubs.c
extern void (*loaderMsgBox)(const char *, ...);
extern int16_t (*loaderSysReq)(int16_t, const char *, const char *, void (*)(void));

// ft2_audio.c
extern bool tmSetupAudioBuffers(uint32_t maxFreq);
extern bool tmSetupSincRender(void);
extern void tmSetTempoScale(double scale);
extern void tmResetSongEnd(bool stopAtEnd);
extern bool tmSongHasEnded(void);
extern void tmRenderFloat(float *stream, uint32_t frames);
extern float tmGetVoiceLevel(int32_t ch);

#define TM_MAX_FREQ 192000
#define TM_AMP 10 // FT2 default amplification
#define TM_RENDER_BLOCK 1024

static bool tmInitDone, tmLoaded;
static int tmInitialBpm = 125;
static uint32_t tmFreq = 48000;

static void noopLoaderMsg(const char *fmt, ...) { (void)fmt; }

static FILE *openMemory(const uint8_t *data, size_t size)
{
#ifdef _WIN32
	FILE *f = tmpfile();
	if (f == NULL)
		return NULL;
	if (fwrite(data, 1, size, f) != size) { fclose(f); return NULL; }
	rewind(f);
	return f;
#else
	return fmemopen((void *)data, size, "rb");
#endif
}

static void clearTmpModule(void)
{
	memset(patternTmp, 0, sizeof (note_t *) * MAX_PATTERNS);
	memset(instrTmp, 0, sizeof (instr_t *) * (1+256));
	memset(&songTmp, 0, sizeof (songTmp));
	for (uint32_t i = 0; i < MAX_PATTERNS; i++)
		patternNumRowsTmp[i] = 64;
}

// the playback half of setupLoadedModule()
static void finalizeLoadedModule(void)
{
	freeAllInstr();
	freeAllPatterns();

	playMode = PLAYMODE_IDLE;
	songPlaying = false;

	for (int32_t i = 0; i < MAX_PATTERNS; i++)
	{
		pattern[i] = patternTmp[i];
		patternNumRows[i] = patternNumRowsTmp[i];
	}

	memcpy(&song, &songTmp, sizeof (song_t));
	fixSongName();

	for (int16_t i = 1; i <= MAX_INST; i++)
	{
		instr[i] = instrTmp[i];
		fixInstrAndSampleNames(i);

		if (instr[i] != NULL)
		{
			sanitizeInstrument(instr[i]);
			for (int32_t j = 0; j < MAX_SMP_PER_INST; j++)
			{
				sample_t *s = &instr[i]->smp[j];
				sanitizeSample(s);
				if (s->dataPtr != NULL)
					fixSample(s);
			}
		}
	}

	if (song.numChannels & 1)
	{
		song.numChannels++;
		if (song.numChannels > MAX_CHANNELS)
			song.numChannels = MAX_CHANNELS;
	}

	song.numChannels = CLAMP(song.numChannels, 2, MAX_CHANNELS);
	song.songLength = CLAMP(song.songLength, 1, MAX_ORDERS);
	song.BPM = CLAMP(song.BPM, MIN_BPM, MAX_BPM);
	song.initialSpeed = song.speed = CLAMP(song.speed, 1, MAX_SPEED);

	if (song.songLoopStart >= song.songLength)
		song.songLoopStart = 0;

	song.globalVolume = 64;

	for (int32_t i = 0; i < MAX_PATTERNS; i++)
	{
		if (patternNumRows[i] <= 0) patternNumRows[i] = 64;
		if (patternNumRows[i] > MAX_PATT_LEN) patternNumRows[i] = MAX_PATT_LEN;
		if (pattern[i] == NULL) continue;

		note_t *p = pattern[i];
		for (int32_t j = 0; j < MAX_PATT_LEN * MAX_CHANNELS; j++, p++)
		{
			if (p->note > 97) p->note = 0;
			if (p->instr > 128) p->instr = 0;
			if (p->efx > 35) { p->efx = 0; p->efxData = 0; }
		}
	}

	resetChannels();
	setSongPos(0, 0, RESET_SONG_TICK);
	setMixerBPM(song.BPM);
	setLinearPeriods(tmpLinearPeriodsFlag);
}

bool tmFt2Init(void)
{
	if (tmInitDone)
		return true;

	memset(&config, 0, sizeof (config));
	config.masterVol = 256;
	config.boostLevel = TM_AMP;
	config.interpolation = INTERPOLATION_SINC16;
	config.audioFreq = tmFreq;
	config.specialFlags = BITDEPTH_32;
	config.specialFlags2 = PRECISE_BPM; // exact BPM, needed for host tempo sync

	audio.freq = tmFreq;
	audio.interpolationType = INTERPOLATION_SINC16;
	audio.volumeRampingFlag = true;

	if (!tmSetupAudioBuffers(TM_MAX_FREQ))
		return false;

	calcPanningTable();
	if (!setupReplayer())
		return false;
	calcMiscReplayerVars();
	if (!setupMixerInterpolationTables())
		return false;
	calcReplayerVars(tmFreq, tmFreq);
	audioSetInterpolationType(INTERPOLATION_SINC16);
	audioSetVolRamp(true);
	setAudioAmp(TM_AMP, 256, true);

	loaderMsgBox = noopLoaderMsg;
	loaderSysReq = NULL;

	tmInitDone = true;
	return true;
}

bool tmFt2Load(const uint8_t *data, size_t size, int format)
{
	if (!tmFt2Init() || data == NULL || size == 0)
		return false;

	tmFt2Stop();

	FILE *f = openMemory(data, size);
	if (f == NULL)
		return false;

	clearTmpModule();
	const bool ok = (format == TM_FT2_FORMAT_MOD) ? loadMOD(f, (uint32_t)size) : loadXM(f, (uint32_t)size);
	fclose(f);

	if (!ok)
	{
		// the loaders leave partial data in the tmp tables on failure
		for (int32_t i = 0; i < MAX_PATTERNS; i++)
		{
			if (patternTmp[i] != NULL) { free(patternTmp[i]); patternTmp[i] = NULL; }
		}
		for (int32_t i = 0; i <= 256; i++)
		{
			if (instrTmp[i] == NULL) continue;
			for (int32_t j = 0; j < MAX_SMP_PER_INST; j++)
				freeSmpData(&instrTmp[i]->smp[j]);
			free(instrTmp[i]);
			instrTmp[i] = NULL;
		}
		return false;
	}

	tmAmigaPan = (format == TM_FT2_FORMAT_MOD);
	finalizeLoadedModule();
	tmInitialBpm = song.BPM;
	tmResetSongEnd(false);
	tmLoaded = true;
	return true;
}

void tmFt2Unload(void)
{
	if (!tmInitDone)
		return;

	tmFt2Stop();
	freeAllInstr();
	freeAllPatterns();
	tmLoaded = false;
}

void tmFt2SetSampleRate(uint32_t freq)
{
	if (freq == 0 || freq > TM_MAX_FREQ)
		return;

	const bool changed = (freq != tmFreq);
	tmFreq = freq;
	if (!tmInitDone || !changed)
		return;

	stopVoices();
	audio.freq = freq;
	config.audioFreq = freq;
	calcReplayerVars(freq, freq);
	setAudioAmp(TM_AMP, 256, true);
	setMixerBPM(song.BPM);
}

void tmFt2SetSinc512(bool enabled)
{
	if (!tmInitDone)
		return;

	if (enabled && !tmSetupSincRender())
		enabled = false;

	const uint8_t type = enabled ? INTERPOLATION_SINC256 : INTERPOLATION_SINC16;
	config.interpolation = type;
	audioSetInterpolationType(type);
}

void tmFt2SetTempoScale(double scale)
{
	if (tmInitDone)
		tmSetTempoScale(scale);
}

void tmFt2Play(int order, bool stopAtEnd)
{
	if (!tmLoaded)
		return;

	if (order < 0) order = 0;
	if (order >= song.songLength) order = song.songLength - 1;

	stopVoices();
	resetChannels();
	song.globalVolume = 64;
	song.BPM = (uint16_t)tmInitialBpm;
	song.speed = song.initialSpeed;
	setSongPos((int16_t)order, 0, RESET_SONG_TICK);
	setMixerBPM(song.BPM);
	tmResetSongEnd(stopAtEnd);

	playMode = PLAYMODE_SONG;
	songPlaying = true;
}

void tmFt2Stop(void)
{
	if (!tmInitDone)
		return;

	songPlaying = false;
	playMode = PLAYMODE_IDLE;
	stopVoices();
}

void tmFt2Render(float *stream, uint32_t frames)
{
	if (!tmLoaded)
	{
		memset(stream, 0, (size_t)frames * 2 * sizeof (float));
		return;
	}

	while (frames > 0)
	{
		const uint32_t n = (frames > TM_RENDER_BLOCK) ? TM_RENDER_BLOCK : frames;
		tmRenderFloat(stream, n);
		stream += n * 2;
		frames -= n;
	}
}

bool tmFt2HasEnded(void) { return tmSongHasEnded(); }
int tmFt2GetOrder(void) { return tmLoaded ? song.songPos : 0; }
int tmFt2GetRow(void) { return tmLoaded ? song.row : 0; }
int tmFt2GetOrderCount(void) { return tmLoaded ? song.songLength : 0; }
int tmFt2GetChannelCount(void) { return tmLoaded ? song.numChannels : 0; }
int tmFt2GetInitialBpm(void) { return tmInitialBpm; }
float tmFt2GetChannelLevel(int channel) { return tmLoaded ? tmGetVoiceLevel(channel) : 0.0f; }
const char *tmFt2GetTitle(void) { return tmLoaded ? song.name : ""; }
