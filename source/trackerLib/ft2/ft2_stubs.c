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
#include "ft2_sysreqs.h"
#include "ft2_hpc.h"
#include "ft2_inst_ed.h"
#include "scopes/ft2_scopes.h"

// ---- real globals normally defined in GUI-heavy files ----
config_t config;
lastChInstr_t lastChInstr[MAX_CHANNELS];
hpcFreq_t hpcFreq = { 1000000000ULL, 0.0, 0.0 };

// loader tmp globals (normally in ft2_module_loader.c)
volatile bool tmpLinearPeriodsFlag = true;
uint8_t tmpBuffer[65536] = {0};
int16_t patternNumRowsTmp[MAX_PATTERNS];
note_t *patternTmp[MAX_PATTERNS];
instr_t *instrTmp[1+256];
song_t songTmp;

void (*loaderMsgBox)(const char *, ...) = NULL;
int16_t (*loaderSysReq)(int16_t, const char *, const char *, void (*)(void)) = NULL;

// ---- sample data helpers (copied verbatim from ft2_sample_ed.c) ----
bool allocateSmpData(sample_t *s, int32_t length, bool sample16Bit)
{
	if (sample16Bit) length <<= 1;
	s->origDataPtr = (int8_t *)malloc(length + SAMPLE_PAD_LENGTH);
	if (s->origDataPtr == NULL) { s->dataPtr = NULL; return false; }
	s->dataPtr = s->origDataPtr + SMP_DAT_OFFSET;
	return true;
}

bool reallocateSmpData(sample_t *s, int32_t length, bool sample16Bit)
{
	if (s->origDataPtr == NULL) return allocateSmpData(s, length, sample16Bit);
	if (sample16Bit) length <<= 1;
	int8_t *newPtr = (int8_t *)realloc(s->origDataPtr, length + SAMPLE_PAD_LENGTH);
	if (newPtr == NULL) return false;
	s->origDataPtr = newPtr;
	s->dataPtr = s->origDataPtr + SMP_DAT_OFFSET;
	return true;
}

void freeSmpData(sample_t *s)
{
	if (s->origDataPtr != NULL) { free(s->origDataPtr); s->origDataPtr = NULL; }
	s->dataPtr = NULL;
	s->isFixed = false;
}

void sanitizeSample(sample_t *s)
{
	if (s == NULL) return;
	if (GET_LOOPTYPE(s->flags) == (LOOP_FORWARD | LOOP_PINGPONG))
		s->flags &= ~LOOP_FORWARD;
	if (s->volume > 64) s->volume = 64;
	s->relativeNote = CLAMP(s->relativeNote, -48, 71);
	s->length = CLAMP(s->length, 0, MAX_SAMPLE_LEN);
	if (s->loopStart < 0 || s->loopLength <= 0 || s->loopStart+s->loopLength > s->length)
	{
		s->loopStart = 0;
		s->loopLength = 0;
		DISABLE_LOOP(s->flags);
	}
}

static int32_t myMod(int32_t a, int32_t b)
{
	int32_t c = a % b;
	return (c < 0) ? (c + b) : c;
}

void fixSample(sample_t *s)
{
	int32_t pos;
	bool backwards;

	if (s->dataPtr == NULL || s->length <= 0) { s->isFixed = false; s->fixedPos = 0; return; }

	const bool sample16Bit = !!(s->flags & SAMPLE_16BIT);
	int16_t *ptr16 = (int16_t *)s->dataPtr;
	uint8_t loopType = GET_LOOPTYPE(s->flags);
	int32_t length = s->length;
	int32_t loopStart = s->loopStart;
	int32_t loopLength = s->loopLength;
	int32_t loopEnd = s->loopStart + s->loopLength;

	if (loopType != 0 && loopLength <= 0) { loopType = 0; loopStart = loopLength = loopEnd = 0; }

	if (sample16Bit) { for (int32_t i = 0; i < MAX_LEFT_TAPS; i++) ptr16[i-MAX_LEFT_TAPS] = ptr16[0]; }
	else { for (int32_t i = 0; i < MAX_LEFT_TAPS; i++) s->dataPtr[i-MAX_LEFT_TAPS] = s->dataPtr[0]; }

	if (loopType == LOOP_DISABLED)
	{
		if (sample16Bit) { for (int32_t i = 0; i < MAX_RIGHT_TAPS; i++) ptr16[length+i] = ptr16[length-1]; }
		else { for (int32_t i = 0; i < MAX_RIGHT_TAPS; i++) s->dataPtr[length+i] = s->dataPtr[length-1]; }
		s->fixedPos = 0; s->isFixed = false; return;
	}

	s->fixedPos = loopEnd;
	s->isFixed = true;

	if (loopType == LOOP_FORWARD)
	{
		if (sample16Bit)
		{
			for (int32_t i = -MAX_LEFT_TAPS; i < MAX_TAPS; i++)
			{ pos = loopStart + myMod(i, loopLength); s->leftEdgeTapSamples16[MAX_LEFT_TAPS+i] = ptr16[pos]; }
			pos = loopStart;
			for (int32_t i = 0; i < MAX_RIGHT_TAPS; i++)
			{ s->fixedSmp[i] = ptr16[loopEnd+i]; ptr16[loopEnd+i] = ptr16[pos]; if (++pos >= loopEnd) pos -= loopLength; }
		}
		else
		{
			for (int32_t i = -MAX_LEFT_TAPS; i < MAX_TAPS; i++)
			{ pos = loopStart + myMod(i, loopLength); s->leftEdgeTapSamples8[MAX_LEFT_TAPS+i] = s->dataPtr[pos]; }
			pos = loopStart;
			for (int32_t i = 0; i < MAX_RIGHT_TAPS; i++)
			{ s->fixedSmp[i] = s->dataPtr[loopEnd+i]; s->dataPtr[loopEnd+i] = s->dataPtr[pos]; if (++pos >= loopEnd) pos -= loopLength; }
		}
	}
	else // pingpong
	{
		if (sample16Bit)
		{
			pos = loopStart; backwards = false;
			for (int32_t i = 0; i < MAX_TAPS; i++)
			{
				if (backwards) { if (pos < loopStart) { pos = loopStart; backwards = false; } }
				else if (pos >= loopEnd) { pos = loopEnd-1; backwards = true; }
				s->leftEdgeTapSamples16[MAX_LEFT_TAPS+i] = ptr16[pos];
				if (backwards) pos--; else pos++;
			}
			for (int32_t i = 0; i < MAX_LEFT_TAPS; i++)
				s->leftEdgeTapSamples16[(MAX_LEFT_TAPS-1)-i] = s->leftEdgeTapSamples16[MAX_LEFT_TAPS+1+i];
			pos = loopEnd-1; backwards = true;
			for (int32_t i = 0; i < MAX_RIGHT_TAPS; i++)
			{
				if (backwards) { if (pos < loopStart) { pos = loopStart; backwards = false; } }
				else if (pos >= loopEnd) { pos = loopEnd-1; backwards = true; }
				s->fixedSmp[i] = ptr16[loopEnd+i]; ptr16[loopEnd+i] = ptr16[pos];
				if (backwards) pos--; else pos++;
			}
		}
		else
		{
			pos = loopStart; backwards = false;
			for (int32_t i = 0; i < MAX_TAPS; i++)
			{
				if (backwards) { if (pos < loopStart) { pos = loopStart; backwards = false; } }
				else if (pos >= loopEnd) { pos = loopEnd-1; backwards = true; }
				s->leftEdgeTapSamples8[MAX_LEFT_TAPS+i] = s->dataPtr[pos];
				if (backwards) pos--; else pos++;
			}
			for (int32_t i = 0; i < MAX_LEFT_TAPS; i++)
				s->leftEdgeTapSamples8[(MAX_LEFT_TAPS-1)-i] = s->leftEdgeTapSamples8[MAX_LEFT_TAPS+1+i];
			pos = loopEnd-1; backwards = true;
			for (int32_t i = 0; i < MAX_RIGHT_TAPS; i++)
			{
				if (backwards) { if (pos < loopStart) { pos = loopStart; backwards = false; } }
				else if (pos >= loopEnd) { pos = loopEnd-1; backwards = true; }
				s->fixedSmp[i] = s->dataPtr[loopEnd+i]; s->dataPtr[loopEnd+i] = s->dataPtr[pos];
				if (backwards) pos--; else pos++;
			}
		}
	}
}

// mix buffer allocation (replaces setupAudioBuffers() without SDL)
bool setupAudioBuffersCli(uint32_t freq)
{
	int32_t maxSamplesPerTick = (int32_t)((freq / (MIN_BPM / 2.5)) + 2);
	audio.fMixBufferL = (float *)calloc(maxSamplesPerTick, sizeof (float));
	audio.fMixBufferR = (float *)calloc(maxSamplesPerTick, sizeof (float));
	return (audio.fMixBufferL != NULL && audio.fMixBufferR != NULL);
}

// ---- SDL no-op stubs (engine references them but we never call audio device) ----
uint64_t SDL_GetPerformanceCounter(void) { return 0; }
const char *SDL_GetError(void) { return ""; }
void SDL_Delay(uint32_t ms) { (void)ms; }
SDL_AudioDeviceID SDL_OpenAudioDevice(const char *a, int b, const SDL_AudioSpec *c, SDL_AudioSpec *d, int e)
{ (void)a;(void)b;(void)c;(void)d;(void)e; return 0; }
void SDL_CloseAudioDevice(uint32_t d) { (void)d; }
void SDL_PauseAudioDevice(uint32_t d, int p) { (void)d;(void)p; }
void SDL_LockAudioDevice(uint32_t d) { (void)d; }
void SDL_UnlockAudioDevice(uint32_t d) { (void)d; }

// ---- loader helpers (copied from ft2_module_loader.c) ----
bool allocateTmpPatt(int32_t pattNum, uint16_t numRows)
{
	patternTmp[pattNum] = (note_t *)calloc((MAX_PATT_LEN * TRACK_WIDTH) + 16, 1);
	if (patternTmp[pattNum] == NULL) return false;
	patternNumRowsTmp[pattNum] = numRows;
	return true;
}

bool allocateTmpInstr(int32_t insNum)
{
	if (instrTmp[insNum] != NULL) return false;
	instr_t *ins = (instr_t *)calloc(1, sizeof (instr_t));
	if (ins == NULL) return false;
	sample_t *s = ins->smp;
	for (int32_t i = 0; i < MAX_SMP_PER_INST; i++, s++) { s->panning = 128; s->volume = 64; }
	instrTmp[insNum] = ins;
	return true;
}

bool tmpPatternEmpty(int32_t pattNum)
{
	if (patternTmp[pattNum] == NULL) return true;
	uint8_t *scanPtr = (uint8_t *)patternTmp[pattNum];
	const uint32_t scanLen = patternNumRowsTmp[pattNum] * TRACK_WIDTH;
	for (uint32_t i = 0; i < scanLen; i++) if (scanPtr[i] != 0) return false;
	return true;
}

void clearUnusedChannels(note_t *pattPtr, int16_t numRows, int32_t numChannels)
{
	if (pattPtr == NULL || numChannels >= MAX_CHANNELS) return;
	const int32_t width = sizeof (note_t) * (MAX_CHANNELS - numChannels);
	note_t *p = &pattPtr[numChannels];
	for (int32_t i = 0; i < numRows; i++, p += MAX_CHANNELS) memset(p, 0, width);
}

// ---- sanitizeInstrument (copied from ft2_inst_ed.c) ----
void sanitizeInstrument(instr_t *ins)
{
	if (ins == NULL) return;
	ins->midiProgram = CLAMP(ins->midiProgram, 0, 127);
	ins->midiBend = CLAMP(ins->midiBend, 0, 36);
	if (ins->midiChannel > 15) ins->midiChannel = 15;
	if (ins->autoVibDepth > 0x0F) ins->autoVibDepth = 0x0F;
	if (ins->autoVibRate > 0x3F) ins->autoVibRate = 0x3F;
	if (ins->autoVibType > 3) ins->autoVibType = 0;
	for (int32_t i = 0; i < 96; i++)
		if (ins->note2SampleLUT[i] >= MAX_SMP_PER_INST) ins->note2SampleLUT[i] = MAX_SMP_PER_INST-1;
	if (ins->volEnvLength > 12) ins->volEnvLength = 12;
	if (ins->volEnvLoopStart > 11) ins->volEnvLoopStart = 11;
	if (ins->volEnvLoopEnd > 11) ins->volEnvLoopEnd = 11;
	if (ins->volEnvSustain > 11) ins->volEnvSustain = 11;
	if (ins->panEnvLength > 12) ins->panEnvLength = 12;
	if (ins->panEnvLoopStart > 11) ins->panEnvLoopStart = 11;
	if (ins->panEnvLoopEnd > 11) ins->panEnvLoopEnd = 11;
	if (ins->panEnvSustain > 11) ins->panEnvSustain = 11;
	for (int32_t i = 0; i < 12; i++)
	{
		if ((uint16_t)ins->volEnvPoints[i][0] > 32767) ins->volEnvPoints[i][0] = 32767;
		if ((uint16_t)ins->panEnvPoints[i][0] > 32767) ins->panEnvPoints[i][0] = 32767;
		if ((uint16_t)ins->volEnvPoints[i][1] > 64) ins->volEnvPoints[i][1] = 64;
		if ((uint16_t)ins->panEnvPoints[i][1] > 63) ins->panEnvPoints[i][1] = 63;
	}
}

// ---- GUI / scope / config-screen no-op stubs ----
void checkMarkLimits(void) {}
void drawSampleC4Hz(void) {}
void drawScrollBar(uint16_t n) { (void)n; }
int32_t getMaxVisibleChannels(void) { return 8; }
void handleScopesFromChQueue(chSyncData_t *a, uint8_t *b) { (void)a;(void)b; }
void hidePushButton(uint16_t n) { (void)n; }
void hideScrollBar(uint16_t n) { (void)n; }
void killPatternIfUnused(uint16_t n) { (void)n; }
int16_t okBox(int16_t a, const char *b, const char *c, void (*d)(void)) { (void)a;(void)b;(void)c;(void)d; return 0; }
void pbSwapInstrBank(void) {}
void resetPlaybackTime(void) { song.playbackSeconds = 0; song.playbackSecondsFrac = 0; }
void setConfigAudioRadioButtonStates(void) {}
void setScrollBarEnd(uint16_t a, uint32_t b) { (void)a;(void)b; }
void setScrollBarPageLength(uint16_t a, uint32_t b) { (void)a;(void)b; }
void setScrollBarPos(uint16_t a, uint32_t b, bool c) { (void)a;(void)b;(void)c; }
void setWavRenderBitDepth(uint8_t b) { (void)b; }
void setWavRenderFrequency(int32_t f) { (void)f; }
void showConfigScreen(void) {}
void showErrorMsgBox(const char *fmt, ...) { (void)fmt; }
void showPushButton(uint16_t n) { (void)n; }
void showScrollBar(uint16_t n) { (void)n; }
void stopAllScopes(void) {}
void updateAdvEdit(void) {}
void updateNewInstrument(void) {}
void updateNewSample(void) {}
void updateTextBoxPointers(void) {}
void updateWavRendererSettings(void) {}
