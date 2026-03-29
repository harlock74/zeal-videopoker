#include <stdint.h>

#include <zvb_sound.h>
#include <zgdk.h>
#include <zgdk/sound/tracker.h>

#include "videopoker.h"
#include "assets.h"
#include "audio.h"

/* Keep audio tuning local to the audio module. */
#define CARD_SOUND 0
#define CARD_SFX_BASE_FREQ 10
#define CARD_SFX_JITTER_MASK 0x03
#define CARD_SFX_DURATION 1
#define CARD_SFX_WAVEFORM WAV_SAWTOOTH

static AudioBindings g_audio = {0};

void audio_bind(const AudioBindings* bindings)
{
    g_audio = *bindings;
}

void start_splash_music(void)
{
    if (!(*g_audio.splash_music_ready)) {
        *g_audio.current_music_mode = 0;
        return;
    }
    if (*g_audio.loaded_music_index != 0) {
        if (load_zmt(g_audio.music_track, 0) == ERR_SUCCESS) {
            *g_audio.loaded_music_index = 0;
        } else {
            *g_audio.current_music_mode = 0;
            return;
        }
    }
    zmt_sound_off();
    zmt_track_reset(g_audio.music_track, 1);
    *g_audio.current_music_mode = 1;
}

void start_game_music(void)
{
    if (*g_audio.game_cards_sfx_mode) {
        *g_audio.current_music_mode = 0;
        return;
    }
    if (!(*g_audio.game_music_ready)) {
        *g_audio.current_music_mode = 0;
        return;
    }
    if (*g_audio.loaded_music_index != 1) {
        if (load_zmt(g_audio.music_track, 1) == ERR_SUCCESS) {
            *g_audio.loaded_music_index = 1;
        } else {
            *g_audio.current_music_mode = 0;
            return;
        }
    }
    zmt_sound_off();
    zmt_track_reset(g_audio.music_track, 1);
    *g_audio.current_music_mode = 2;
}

void tick_current_music(void)
{
    if (*g_audio.current_music_mode == 1 && *g_audio.splash_music_ready) {
        /* Splash track authored with arrangement flow. */
        zmt_tick(g_audio.music_track, 1);
    } else if (*g_audio.current_music_mode == 2 && *g_audio.game_music_ready) {
        /* Gameplay track uses arrangement flow (zmt_tick(..., 1)). */
        zmt_tick(g_audio.music_track, 1);
    }
}

void stop_current_music(void)
{
    zmt_sound_off();
    *g_audio.current_music_mode = 0;
}

void apply_game_audio_mode(void)
{
    if (*g_audio.game_cards_sfx_mode) {
        /* Card-SFX-only mode: silence gameplay music. */
        if (*g_audio.current_music_mode == 2) {
            stop_current_music();
        }
        /*
         * After tracker shutdown, restore sound/tracker runtime state so
         * plain sound_play() effects remain audible in SFX-only mode.
         */
        zmt_reset(VOL_50);
    } else {
        /* Music-only mode: ensure gameplay track is active outside splash. */
        if (*g_audio.state != STATE_BET || !(*g_audio.pending_bankrupt_reset)) {
            start_game_music();
        }
    }
}

void play_card_place_sound(void)
{
    if (*g_audio.game_cards_sfx_mode == 0) {
        return;
    }
    /*
     * Short, low-pitch pluck with tiny random jitter to avoid identical
     * transients per card while still sounding like the same action.
     */
    uint16_t freq = (uint16_t)(CARD_SFX_BASE_FREQ + (rand8_quick() & CARD_SFX_JITTER_MASK));
    Sound* tap = sound_get(CARD_SOUND);
    if (tap != NULL) {
        tap->waveform = CARD_SFX_WAVEFORM;
    }
    tap = sound_play(CARD_SOUND, freq, CARD_SFX_DURATION);
    if (tap != NULL) {
        tap->waveform = CARD_SFX_WAVEFORM;
    }
    *g_audio.entropy ^= (uint16_t)(freq << 1);
}
