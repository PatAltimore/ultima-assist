/*
 * sound_web.cpp -- Web Audio sound backend for the browser build
 * (ultima-assist).  Replaces sound_faun.cpp, which needs the Faun and
 * libvorbis libraries.
 *
 * Sounds and music are read from the game module, decoded here (Ogg Vorbis
 * with stb_vorbis, WAV by hand, rFX effects with sfx_gen) and handed to the
 * Web Audio API through sound_web.js.  Effects are decoded once and kept;
 * music is streamed in small chunks pulled by a timer in the page
 * (web_music_pump).  Nothing is audible until the page unlocks audio from a
 * user gesture.
 */

#ifdef __EMSCRIPTEN__

#include <emscripten.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sound.h"

#include "config.h"
#include "context.h"
#include "debug.h"
#include "error.h"
#include "event.h"
#include "settings.h"
#include "xu4.h"

// stb_vorbis is a single .c file; HEADER_ONLY gives just its declarations.
#define STB_VORBIS_NO_STDIO
#define STB_VORBIS_NO_PUSHDATA_API
#define STB_VORBIS_HEADER_ONLY
#include "stb_vorbis.c"

extern "C" {
#include "sfx_gen.h"

void web_audio_init(void);
int  web_audio_ready(void);
void web_audio_set_buffer(int id, const int16_t* pcm, int frames, int channels, int rate);
void web_audio_set_buffer_f32(int id, const float* pcm, int frames, int rate);
void web_audio_play(int id, int limitMs);
void web_audio_stop_sounds(void);
void web_audio_music_begin(int fadeInMs);
void web_audio_music_end(int fadeOutMs);
void web_audio_music_queue(const float* left, const float* right, int frames, int rate);
int  web_audio_music_buffered_ms(void);
void web_audio_set_volumes(float music, float sfx);
void web_audio_suspend(int on);

// sfx_gen needs a random source; a simple xorshift seeded per effect keeps
// each effect sounding the same every time.
static uint32_t sfxRng = 1;
int sfx_random(int range)
{
    sfxRng ^= sfxRng << 13;
    sfxRng ^= sfxRng >> 17;
    sfxRng ^= sfxRng << 5;
    return int(sfxRng % uint32_t(range));
}
}

#define BUFFER_LIMIT        SOUND_MAX
#define BUFFER_MS_FAILED    1
#define MUSIC_CHUNK         8192
#define MUSIC_AHEAD_MS      1500

static int currentTrack = MUSIC_NONE;
static int musicEnabled = 1;
static int musicFadeMs;
static float soundVolume = 0.0f;
static float musicVolume = 0.0f;
static uint16_t bufferMs[BUFFER_LIMIT];

static stb_vorbis* musicVorbis;
static uint8_t* musicData;
static int musicRate = 44100;
static int musicChannels = 2;

/* Read a file stored inside the game module. */
static uint8_t* readEntry(const CDIEntry* ent, size_t* size)
{
    FILE* fp = fopen(xu4.config->modulePath(ent), "rb");
    if (! fp)
        return NULL;
    uint8_t* buf = (uint8_t*) malloc(ent->bytes);
    if (buf && fseek(fp, ent->offset, SEEK_SET) == 0 &&
        fread(buf, 1, ent->bytes, fp) == ent->bytes) {
        fclose(fp);
        *size = ent->bytes;
        return buf;
    }
    fclose(fp);
    free(buf);
    return NULL;
}

static inline uint32_t rd32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t) p[3] << 24); }
static inline uint16_t rd16(const uint8_t* p) { return uint16_t(p[0] | (p[1] << 8)); }

/* Decode a PCM WAV file into 16-bit samples handed to JavaScript.  Returns frames or 0. */
static int loadWav(int id, const uint8_t* d, size_t n, int* rateOut)
{
    int channels = 0, bits = 0, rate = 0;
    const uint8_t* data = NULL;
    size_t dataLen = 0;

    if (n < 12 || memcmp(d + 8, "WAVE", 4) != 0)
        return 0;
    size_t pos = 12;
    while (pos + 8 <= n) {
        uint32_t len = rd32(d + pos + 4);
        const uint8_t* body = d + pos + 8;
        if (pos + 8 + len > n)
            len = uint32_t(n - pos - 8);
        if (memcmp(d + pos, "fmt ", 4) == 0 && len >= 16) {
            if (rd16(body) != 1)            // PCM only
                return 0;
            channels = rd16(body + 2);
            rate = int(rd32(body + 4));
            bits = rd16(body + 14);
        } else if (memcmp(d + pos, "data", 4) == 0) {
            data = body;
            dataLen = len;
            break;
        }
        pos += 8 + len + (len & 1);
    }
    if (! data || channels < 1 || channels > 2 || rate <= 0 || (bits != 8 && bits != 16))
        return 0;

    int bytesPer = bits / 8;
    int frames = int(dataLen / (bytesPer * channels));
    int16_t* pcm = (int16_t*) malloc(size_t(frames) * channels * sizeof(int16_t));
    if (! pcm)
        return 0;
    for (int i = 0; i < frames * channels; ++i) {
        if (bits == 16)
            pcm[i] = int16_t(rd16(data + i * 2));
        else
            pcm[i] = int16_t((int(data[i]) - 128) << 8);
    }
    web_audio_set_buffer(id, pcm, frames, channels, rate);
    free(pcm);
    *rateOut = rate;
    return frames;
}

/* Decode (or synthesize) one sound effect.  Returns its length in ms, or BUFFER_MS_FAILED. */
static int loadSoundBuffer(int sound)
{
    int ms = BUFFER_MS_FAILED;
    const CDIEntry* ent = xu4.config->soundFile(sound);
    size_t n = 0;
    uint8_t* d = ent ? readEntry(ent, &n) : NULL;

    if (d && n > 12) {
        int frames = 0, rate = 0;

        if (memcmp(d, "OggS", 4) == 0) {
            int channels = 0;
            short* pcm = NULL;
            frames = stb_vorbis_decode_memory(d, int(n), &channels, &rate, &pcm);
            if (frames > 0 && pcm) {
                web_audio_set_buffer(sound, pcm, frames, channels, rate);
            } else {
                frames = 0;
            }
            free(pcm);
        } else if (memcmp(d, "RIFF", 4) == 0) {
            frames = loadWav(sound, d, n, &rate);
        } else if (memcmp(d, "rFX ", 4) == 0 && rd16(d + 4) == 200 &&
                   n >= 8 + sizeof(SfxParams)) {
            SfxParams sp;
            memcpy(&sp, d + 8, sizeof(sp));
            SfxSynth* synth = sfx_allocSynth(SFX_F32, 44100, 6);
            if (synth) {
                sfxRng = sp.randSeed ? sp.randSeed : 1;
                frames = sfx_generateWave(synth, &sp);
                if (frames > 0) {
                    web_audio_set_buffer_f32(sound, synth->samples.f, frames, 44100);
                    rate = 44100;
                }
                free(synth);
            }
        }

        if (frames > 0 && rate > 0) {
            ms = int((int64_t(frames) * 1000) / rate);
            if (ms <= BUFFER_MS_FAILED)
                ms = BUFFER_MS_FAILED + 1;
        }
    }
    free(d);

    bufferMs[sound] = uint16_t(ms > 65535 ? 65535 : ms);
    return ms;
}

static void music_close()
{
    if (musicVorbis) {
        stb_vorbis_close(musicVorbis);
        musicVorbis = NULL;
    }
    free(musicData);
    musicData = NULL;
}

static void applyVolumes()
{
    web_audio_set_volumes(musicVolume, soundVolume);
}

int soundInit()
{
    currentTrack = MUSIC_NONE;
    musicFadeMs = 0;
    memset(bufferMs, 0, sizeof(bufferMs));
    web_audio_init();
    musicEnabled = 1;
    musicSetVolume(xu4.settings->musicVol);
    soundSetVolume(xu4.settings->soundVol);
    return 1;
}

void soundDelete()
{
    music_close();
    web_audio_music_end(0);
}

void soundSuspend(int halt)
{
    web_audio_suspend(halt);
}

void soundFreeResourceGroup(uint16_t)
{
    // Decoded effects are small; keep them for the whole session.
}

void soundPlay(Sound sound, int limitMSec)
{
    ASSERT(sound < SOUND_MAX, "Invalid soundPlay() id");

    if (soundVolume <= 0.0f)
        return;
    if (bufferMs[sound] == 0)
        loadSoundBuffer(sound);
    if (bufferMs[sound] > BUFFER_MS_FAILED)
        web_audio_play(sound, limitMSec);
}

/* No voice recordings ship with the Ultima IV module. */
void soundSpeakLine(int, int, bool) {}

int soundDuration(Sound sound)
{
    int ms = bufferMs[sound];
    if (ms == 0)
        ms = loadSoundBuffer(sound);
    return (ms <= BUFFER_MS_FAILED) ? 0 : ms;
}

void soundStop()
{
    web_audio_stop_sounds();
}

/*
 * Start a music track (always looping).  Returns true if it begins from the
 * start; false if it is already playing or cannot be loaded.
 */
static bool music_start(int music, int fadeInMs)
{
    ASSERT(music < MUSIC_MAX, "Invalid music_start() track id");

    if (music == currentTrack)
        return false;

    const CDIEntry* ent = xu4.config->musicFile(music);
    if (! ent)
        return false;

    size_t n = 0;
    uint8_t* d = readEntry(ent, &n);
    if (! d)
        return false;

    int err = 0;
    stb_vorbis* v = stb_vorbis_open_memory(d, int(n), &err, NULL);
    if (! v) {
        free(d);
        return false;
    }

    music_close();
    musicVorbis = v;
    musicData = d;
    stb_vorbis_info info = stb_vorbis_get_info(v);
    musicRate = int(info.sample_rate);
    musicChannels = info.channels;

    web_audio_music_begin(fadeInMs);
    currentTrack = music;
    return true;
}

/* Called by the page's timer: top up the music queue while audio is running. */
extern "C" EMSCRIPTEN_KEEPALIVE void web_music_pump(void)
{
    static float left[MUSIC_CHUNK], right[MUSIC_CHUNK];

    if (! musicVorbis)
        return;

    for (int guard = 0; guard < 6 && web_audio_music_buffered_ms() < MUSIC_AHEAD_MS; ++guard) {
        float* outs[2] = { left, right };
        int got = stb_vorbis_get_samples_float(musicVorbis, musicChannels > 1 ? 2 : 1, outs, MUSIC_CHUNK);
        if (got <= 0) {                         // end of track: loop
            stb_vorbis_seek_start(musicVorbis);
            continue;
        }
        if (musicChannels < 2)
            memcpy(right, left, size_t(got) * sizeof(float));
        web_audio_music_queue(left, right, got, musicRate);
    }
}

void musicPlay(int track)
{
    if (musicEnabled && musicVolume > 0.0f)
        music_start(track, 0);
}

void musicPlayLocale()
{
    musicPlay(c->location->map->music);
}

void musicStop()
{
    currentTrack = MUSIC_NONE;
    music_close();
    web_audio_music_end(0);
}

void musicFadeOut(int msec)
{
    if (currentTrack != MUSIC_NONE) {
        currentTrack = MUSIC_NONE;
        music_close();
        web_audio_music_end(xu4.settings->volumeFades ? msec : 0);
    }
}

void musicFadeIn(int msec, bool loadFromMap)
{
    int fade = (xu4.settings->volumeFades && msec > 0) ? msec : 0;

    if (loadFromMap || currentTrack == MUSIC_NONE)
        music_start(c->location->map->music, fade);
}

void musicUpdate() {}

void musicSetVolume(int volume)
{
    musicVolume = float(volume) / MAX_VOLUME;
    applyVolumes();
}

int musicVolumeDec()
{
    if (xu4.settings->musicVol > 0)
        musicSetVolume(--xu4.settings->musicVol);
    return (xu4.settings->musicVol * 100 / MAX_VOLUME);
}

int musicVolumeInc()
{
    if (xu4.settings->musicVol < MAX_VOLUME)
        musicSetVolume(++xu4.settings->musicVol);
    return (xu4.settings->musicVol * 100 / MAX_VOLUME);
}

bool musicToggle()
{
    musicEnabled = ! musicEnabled;
    if (musicEnabled)
        musicFadeIn(1000, true);
    else
        musicFadeOut(1000);
    return musicEnabled;
}

void soundSetVolume(int volume)
{
    soundVolume = float(volume) / MAX_VOLUME;
    applyVolumes();
}

int soundVolumeDec()
{
    if (xu4.settings->soundVol > 0)
        soundSetVolume(--xu4.settings->soundVol);
    return (xu4.settings->soundVol * 100 / MAX_VOLUME);
}

int soundVolumeInc()
{
    if (xu4.settings->soundVol < MAX_VOLUME)
        soundSetVolume(++xu4.settings->soundVol);
    return (xu4.settings->soundVol * 100 / MAX_VOLUME);
}

#endif  // __EMSCRIPTEN__
