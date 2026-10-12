/*
  Simple DirectMedia Layer
  Copyright (C) 1997-2026 Sam Lantinga <slouken@libsdl.org>

  This software is provided 'as-is', without any express or implied
  warranty.  In no event will the authors be held liable for any damages
  arising from the use of this software.

  Permission is granted to anyone to use this software for any purpose,
  including commercial applications, and to alter it and redistribute it
  freely, subject to the following restrictions:

  1. The origin of this software must not be misrepresented; you must not
     claim that you wrote the original software. If you use this software
     in a product, an acknowledgment in the product documentation would be
     appreciated but is not required.
  2. Altered source versions must be plainly marked as such, and must not be
     misrepresented as being the original software.
  3. This notice may not be removed or altered from any source distribution.
*/
/* PS5 audio for SDL3: the SDL2 PS5 port's sceAudioOut backend (ps5-payload-dev/SDL, src/audio/ps5)
 * moved to SDL3's driver interface. Altered source: Outsider PS5. */
#ifndef SDL_ps5audio_h_
#define SDL_ps5audio_h_

#include "../SDL_sysaudio.h"

#define PS5AUDIO_NUM_BUFFERS 2

struct SDL_PrivateAudioData
{
    int32_t handle;
    Uint8 *rawbuf;
    Uint8 *mixbufs[PS5AUDIO_NUM_BUFFERS];
    int next_buffer;
};

#define PROSPERO_AUDIO_OUT_PORT_TYPE_MAIN 0

#define PROSPERO_AUDIO_OUT_PARAM_FORMAT_S16_MONO     0
#define PROSPERO_AUDIO_OUT_PARAM_FORMAT_S16_STEREO   1
#define PROSPERO_AUDIO_OUT_PARAM_FORMAT_FLOAT_MONO   3
#define PROSPERO_AUDIO_OUT_PARAM_FORMAT_FLOAT_STEREO 4

#define PROSPERO_USER_SERVICE_USER_ID_SYSTEM 0xFF

int32_t sceAudioOutInit(void);
int32_t sceAudioOutOpen(int32_t userId, int32_t type, int32_t index,
                        uint32_t len, uint32_t freq, uint32_t param);
int32_t sceAudioOutOutput(int32_t handle, const void *p);
int32_t sceAudioOutClose(int32_t handle);

#endif /* SDL_ps5audio_h_ */
