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
 * moved to SDL3's driver interface. Altered source: Outsider PS5.
 * The main port takes 48 kHz S16 or float, mono or stereo; SDL's audio streams convert the rest. */
#include "SDL_internal.h"

#ifdef SDL_AUDIO_DRIVER_PS5

#include "SDL_ps5audio.h"

static int PS5AUDIO_SampleFrames(int frames)
{
    /* the port's supported grain sizes */
    static const int sizes[] = { 2048, 1792, 1536, 1280, 1024, 768, 512 };
    for (int i = 0; i < (int)SDL_arraysize(sizes); ++i) {
        if (frames >= sizes[i]) {
            return sizes[i];
        }
    }
    return 256;
}

static bool PS5AUDIO_OpenDevice(SDL_AudioDevice *device)
{
    const SDL_AudioFormat *closefmts;
    SDL_AudioFormat test_format;
    int fmt = 0;

    device->hidden = (struct SDL_PrivateAudioData *)SDL_calloc(1, sizeof(*device->hidden));
    if (!device->hidden) {
        return false;
    }
    device->hidden->handle = -1;

    device->spec.channels = device->spec.channels == 1 ? 1 : 2;
    closefmts = SDL_ClosestAudioFormats(device->spec.format);
    while ((test_format = *(closefmts++)) != 0) {
        if (test_format == SDL_AUDIO_S16LE) {
            fmt = device->spec.channels == 1 ? PROSPERO_AUDIO_OUT_PARAM_FORMAT_S16_MONO
                                             : PROSPERO_AUDIO_OUT_PARAM_FORMAT_S16_STEREO;
            break;
        }
        if (test_format == SDL_AUDIO_F32LE) {
            fmt = device->spec.channels == 1 ? PROSPERO_AUDIO_OUT_PARAM_FORMAT_FLOAT_MONO
                                             : PROSPERO_AUDIO_OUT_PARAM_FORMAT_FLOAT_STEREO;
            break;
        }
    }
    if (!test_format) {
        return SDL_SetError("ps5: unsupported audio format");
    }
    device->spec.format = test_format;
    device->spec.freq = 48000;
    device->sample_frames = PS5AUDIO_SampleFrames(device->sample_frames);
    SDL_UpdatedAudioDeviceFormat(device);

    device->hidden->rawbuf = (Uint8 *)SDL_aligned_alloc(64, device->buffer_size * PS5AUDIO_NUM_BUFFERS);
    if (!device->hidden->rawbuf) {
        return false;
    }
    SDL_memset(device->hidden->rawbuf, 0, device->buffer_size * PS5AUDIO_NUM_BUFFERS);
    for (int i = 0; i < PS5AUDIO_NUM_BUFFERS; ++i) {
        device->hidden->mixbufs[i] = device->hidden->rawbuf + i * device->buffer_size;
    }

    device->hidden->handle = sceAudioOutOpen(PROSPERO_USER_SERVICE_USER_ID_SYSTEM, PROSPERO_AUDIO_OUT_PORT_TYPE_MAIN,
                                             0, (uint32_t)device->sample_frames, 48000, (uint32_t)fmt);
    if (device->hidden->handle < 0) {
        return SDL_SetError("sceAudioOutOpen: 0x%08x", (unsigned)device->hidden->handle);
    }
    return true;
}

static bool PS5AUDIO_WaitDevice(SDL_AudioDevice *device)
{
    return true; /* sceAudioOutOutput blocks until the port takes the next grain */
}

static bool PS5AUDIO_PlayDevice(SDL_AudioDevice *device, const Uint8 *buffer, int buflen)
{
    return sceAudioOutOutput(device->hidden->handle, buffer) >= 0;
}

static Uint8 *PS5AUDIO_GetDeviceBuf(SDL_AudioDevice *device, int *buffer_size)
{
    /* the port may still be reading the previous grain: alternate buffers */
    Uint8 *buf = device->hidden->mixbufs[device->hidden->next_buffer];
    device->hidden->next_buffer = (device->hidden->next_buffer + 1) % PS5AUDIO_NUM_BUFFERS;
    return buf;
}

static void PS5AUDIO_CloseDevice(SDL_AudioDevice *device)
{
    if (!device->hidden) {
        return;
    }
    if (device->hidden->handle >= 0) {
        sceAudioOutOutput(device->hidden->handle, NULL); /* drain */
        sceAudioOutClose(device->hidden->handle);
    }
    SDL_aligned_free(device->hidden->rawbuf);
    SDL_free(device->hidden);
    device->hidden = NULL;
}

static bool PS5AUDIO_Init(SDL_AudioDriverImpl *impl)
{
    static bool initialized = false;
    if (!initialized) {
        int err = sceAudioOutInit();
        if (err != 0 && err != (int)0x8026000e) { /* already initialized */
            return SDL_SetError("sceAudioOutInit: 0x%08x", (unsigned)err);
        }
        initialized = true;
    }
    impl->OpenDevice = PS5AUDIO_OpenDevice;
    impl->WaitDevice = PS5AUDIO_WaitDevice;
    impl->PlayDevice = PS5AUDIO_PlayDevice;
    impl->GetDeviceBuf = PS5AUDIO_GetDeviceBuf;
    impl->CloseDevice = PS5AUDIO_CloseDevice;
    impl->OnlyHasDefaultPlaybackDevice = true;
    return true;
}

AudioBootStrap PS5AUDIO_bootstrap = {
    "ps5", "PS5 audio driver", PS5AUDIO_Init, false, false
};

#endif /* SDL_AUDIO_DRIVER_PS5 */
