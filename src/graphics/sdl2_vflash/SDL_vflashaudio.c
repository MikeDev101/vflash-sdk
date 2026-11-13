#include "SDL_internal.h"

#include "SDL_audio.h"
#include "audio/SDL_audio_c.h"
#include "audio/SDL_sysaudio.h"

#include "vflash/audio.h"

#define VFLASH_AUDIO_DRIVER_NAME "vflash"

static SDL_AudioDevice *s_current_audio_device = NULL;
static Uint8 s_work_buffer[4096] __attribute__((aligned(64)));

static int VFLASHAUDIO_OpenDevice(SDL_AudioDevice *_this, const char *devname)
{
    (void)devname;
    if (_this->iscapture) {
        return SDL_SetError("Audio capture not supported on V.Flash");
    }

    // Device natively supports 16-bit signed little-endian PCM
    _this->spec.format = AUDIO_S16LSB;

    if (_this->spec.channels > 2) {
        _this->spec.channels = 2;
    } else if (_this->spec.channels < 1) {
        _this->spec.channels = 1;
    }

    if (_this->spec.freq != 11025 && _this->spec.freq != 22050 && _this->spec.freq != 44100) {
        _this->spec.freq = 44100;
    }

    if (_this->spec.samples == 0 || _this->spec.samples > 1024) {
        _this->spec.samples = 1024;
    }

    SDL_CalculateAudioSpec(&_this->spec);

    vflash_audio_config_t config;
    config.sample_rate = _this->spec.freq;
    config.channels = _this->spec.channels;
    config.format = VFLASH_AUDIO_FMT_S16LE;
    config.buffer_samples = 4096;

    if (!vflash_audio_init(&config)) {
        return SDL_SetError("Failed to initialize V.Flash native audio hardware");
    }

    s_current_audio_device = _this;
    return 0;
}

static void VFLASHAUDIO_CloseDevice(SDL_AudioDevice *_this)
{
    vflash_audio_shutdown();
    if (s_current_audio_device == _this) {
        s_current_audio_device = NULL;
    }
}

static void VFLASHAUDIO_LockOrUnlockDevice(SDL_AudioDevice *device)
{
    (void)device;
    // Freestanding single-threaded runtime: no mutex lock required
}

void VFLASH_AudioPump(void)
{
    if (!s_current_audio_device) return;
    if (!SDL_AtomicGet(&s_current_audio_device->enabled)) return;
    if (SDL_AtomicGet(&s_current_audio_device->paused)) return;

    vflash_audio_update();

    Uint32 packet_size = s_current_audio_device->spec.size;
    if (packet_size == 0 || packet_size > sizeof(s_work_buffer)) {
        packet_size = sizeof(s_work_buffer);
    }

    Uint32 avail = vflash_audio_get_available_bytes();

    while (avail >= packet_size) {
        SDL_AudioCallback callback = s_current_audio_device->callbackspec.callback;
        int stream_len = (int)s_current_audio_device->callbackspec.size;
        if (stream_len <= 0 || (size_t)stream_len > sizeof(s_work_buffer)) {
            stream_len = (int)packet_size;
        }

        if (!s_current_audio_device->stream) {
            callback(s_current_audio_device->callbackspec.userdata, s_work_buffer, stream_len);
            Uint32 written = vflash_audio_write(s_work_buffer, (Uint32)stream_len);
            if (written == 0) break;
        } else {
            while (SDL_AudioStreamAvailable(s_current_audio_device->stream) < (int)packet_size) {
                callback(s_current_audio_device->callbackspec.userdata, s_work_buffer, stream_len);
                if (SDL_AudioStreamPut(s_current_audio_device->stream, s_work_buffer, stream_len) == -1) {
                    SDL_AudioStreamClear(s_current_audio_device->stream);
                    break;
                }
            }
            int got = SDL_AudioStreamGet(s_current_audio_device->stream, s_work_buffer, (int)packet_size);
            if (got > 0) {
                Uint32 written = vflash_audio_write(s_work_buffer, (Uint32)got);
                if (written == 0) break;
            } else {
                break;
            }
        }

        avail = vflash_audio_get_available_bytes();
    }
}

static SDL_bool VFLASHAUDIO_Init(SDL_AudioDriverImpl *impl)
{
    impl->OpenDevice = VFLASHAUDIO_OpenDevice;
    impl->CloseDevice = VFLASHAUDIO_CloseDevice;
    impl->LockDevice = VFLASHAUDIO_LockOrUnlockDevice;
    impl->UnlockDevice = VFLASHAUDIO_LockOrUnlockDevice;

    impl->OnlyHasDefaultOutputDevice = SDL_TRUE;
    impl->ProvidesOwnCallbackThread = SDL_TRUE;

    return SDL_TRUE;
}

AudioBootStrap VFLASH_AudioBootStrap = {
    VFLASH_AUDIO_DRIVER_NAME,
    "VTech V.Flash Native Audio Driver",
    VFLASHAUDIO_Init,
    SDL_FALSE
};
