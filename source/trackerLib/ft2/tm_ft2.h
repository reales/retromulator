// Trackermeister: plain C API over the vendored Fasttracker 2 replayer.
// The replayer keeps its state in globals, so one module plays per process.
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

enum { TM_FT2_FORMAT_XM = 0, TM_FT2_FORMAT_MOD = 1 };

bool tmFt2Init(void);
bool tmFt2Load(const uint8_t *data, size_t size, int format);
void tmFt2Unload(void);

void tmFt2SetSampleRate(uint32_t freq);
void tmFt2SetSinc512(bool enabled);
void tmFt2SetTempoScale(double scale);

void tmFt2Play(int order, bool stopAtEnd);
void tmFt2Stop(void);
void tmFt2Render(float *interleavedStereo, uint32_t frames);

bool tmFt2HasEnded(void);
int tmFt2GetOrder(void);
int tmFt2GetRow(void);
int tmFt2GetOrderCount(void);
int tmFt2GetChannelCount(void);
int tmFt2GetInitialBpm(void);
float tmFt2GetChannelLevel(int channel);
const char *tmFt2GetTitle(void);

#ifdef __cplusplus
}
#endif
