/* Trackermeister: plain C API over the vendored Schism Tracker player (S3M). */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct tm_schism tm_schism_t;

tm_schism_t *tmSchismCreate(const uint8_t *data, size_t size, uint32_t freq);
void tmSchismDestroy(tm_schism_t *t);

void tmSchismSetSampleRate(tm_schism_t *t, uint32_t freq);
void tmSchismSetSinc512(tm_schism_t *t, bool enabled);
void tmSchismSetTempoScale(tm_schism_t *t, double scale);

void tmSchismPlay(tm_schism_t *t, int order, bool stopAtEnd);
void tmSchismStop(tm_schism_t *t);
void tmSchismRender(tm_schism_t *t, float *interleavedStereo, uint32_t frames);

bool tmSchismHasEnded(const tm_schism_t *t);
int tmSchismGetOrder(const tm_schism_t *t);
int tmSchismGetRow(const tm_schism_t *t);
int tmSchismGetOrderCount(const tm_schism_t *t);
int tmSchismGetChannelCount(const tm_schism_t *t);
int tmSchismGetInitialBpm(const tm_schism_t *t);
float tmSchismGetChannelLevel(tm_schism_t *t, int channel);
const char *tmSchismGetTitle(const tm_schism_t *t);

#ifdef __cplusplus
}
#endif
