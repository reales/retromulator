// Minimal SDL2 shim for headless FT2 CLI. Provides only the types/declarations
// the compiled engine source needs. SDL functions are stubbed in cli/stubs.c.
#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <math.h>

// SDL keycodes used by data tables (equal to ASCII for these)
#define SDLK_0 '0'
#define SDLK_1 '1'
#define SDLK_2 '2'
#define SDLK_3 '3'
#define SDLK_4 '4'
#define SDLK_5 '5'
#define SDLK_6 '6'
#define SDLK_7 '7'
#define SDLK_8 '8'
#define SDLK_9 '9'
#define SDLK_MINUS '-'
#define SDLK_PLUS '+'
#define SDLK_a 'a'
#define SDLK_b 'b'
#define SDLK_c 'c'
#define SDLK_d 'd'
#define SDLK_e 'e'
#define SDLK_f 'f'
#define SDLK_g 'g'
#define SDLK_h 'h'
#define SDLK_i 'i'
#define SDLK_j 'j'
#define SDLK_k 'k'
#define SDLK_l 'l'
#define SDLK_m 'm'
#define SDLK_n 'n'
#define SDLK_o 'o'
#define SDLK_p 'p'
#define SDLK_q 'q'
#define SDLK_r 'r'
#define SDLK_s 's'
#define SDLK_t 't'
#define SDLK_u 'u'
#define SDLK_v 'v'
#define SDLK_w 'w'
#define SDLK_x 'x'
#define SDLK_y 'y'
#define SDLK_z 'z'

typedef uint8_t Uint8;
typedef int8_t Sint8;
typedef uint16_t Uint16;
typedef int16_t Sint16;
typedef uint32_t Uint32;
typedef int32_t Sint32;
typedef uint64_t Uint64;
typedef int64_t Sint64;

typedef uint32_t SDL_AudioDeviceID;
typedef uint16_t SDL_AudioFormat;
typedef struct SDL_Thread SDL_Thread;
typedef struct SDL_Window SDL_Window;
typedef struct SDL_Renderer SDL_Renderer;
typedef struct SDL_Texture SDL_Texture;
typedef struct SDL_Surface SDL_Surface;
typedef struct SDL_Cursor SDL_Cursor;
typedef struct SDL_mutex SDL_mutex;
typedef struct SDL_sem SDL_sem;
typedef int32_t SDL_Keycode;
typedef int32_t SDL_Scancode;
typedef uint32_t SDL_threadID;
typedef int (*SDL_ThreadFunction)(void *data);
typedef struct SDL_Rect { int x, y, w, h; } SDL_Rect;
typedef struct SDL_Point { int x, y; } SDL_Point;
typedef union SDL_Event SDL_Event;

#define AUDIO_S16 0x8010
#define AUDIO_F32 0x8120
#define SDL_AUDIO_ALLOW_FREQUENCY_CHANGE 0x00000002

typedef void (*SDL_AudioCallback)(void *userdata, Uint8 *stream, int len);

typedef struct SDL_AudioSpec
{
	int freq;
	SDL_AudioFormat format;
	Uint8 channels, silence;
	Uint16 samples;
	Uint16 padding;
	Uint32 size;
	SDL_AudioCallback callback;
	void *userdata;
} SDL_AudioSpec;

Uint64 SDL_GetPerformanceCounter(void);
const char *SDL_GetError(void);
void SDL_Delay(Uint32 ms);
SDL_AudioDeviceID SDL_OpenAudioDevice(const char *device, int iscapture,
	const SDL_AudioSpec *desired, SDL_AudioSpec *obtained, int allowed_changes);
void SDL_CloseAudioDevice(SDL_AudioDeviceID dev);
void SDL_PauseAudioDevice(SDL_AudioDeviceID dev, int pause_on);
void SDL_LockAudioDevice(SDL_AudioDeviceID dev);
void SDL_UnlockAudioDevice(SDL_AudioDeviceID dev);
