#include <stdint.h>

#include "audio.h"

#include <zvb_sound.h>
#include <zgdk.h>
#include <zgdk/sound/tracker.h>
#include <core.h>

#include "app_state.h"
#include "assets.h"

/* Keep audio tuning local to the audio module. */
#define CARD_SOUND 0
#define CARD_SFX_BASE_FREQ 10
#define CARD_SFX_JITTER_MASK 0x03
#define CARD_SFX_DURATION 1
#define CARD_SFX_WAVEFORM WAV_SAWTOOTH

/* Tracker storage owned by audio module. */
static pattern_t music_pattern0;
static pattern_t music_pattern1;
static pattern_t music_pattern2;
static pattern_t music_pattern3;
static pattern_t music_pattern4;
static pattern_t music_pattern5;
static pattern_t music_pattern6;
static pattern_t music_pattern7;
static track_t music_track = {
    .title = {(char)ZMT_INDEX_NONE, '\0'},
    .patterns = {
        &music_pattern0,
        &music_pattern1,
        &music_pattern2,
        &music_pattern3,
        &music_pattern4,
        &music_pattern5,
        &music_pattern6,
        &music_pattern7,
    }
};

/* Audio state owned by this module. */
static uint8_t music_playing = 0; /* 0=stopped, 1=playing */
/* 0 = music-only (card SFX muted), 1 = card-SFX-only (gameplay track paused). */
static uint8_t game_cards_sfx_mode = 0;

void start_splash_music(void)
{
    /* Stop any currently playing tracker state before switching assets. */
    zmt_sound_off();

    if ((uint8_t)music_track.title[0] != ZMT_INDEX_SPLASH) {
        if (load_zmt(&music_track, ZMT_INDEX_SPLASH) != ERR_SUCCESS) {
            music_playing = 0;
            return;
        }
    }
    zmt_track_reset(&music_track, 1);
    music_playing = 1;
}

void start_game_music(void)
{
    /* Stop any currently playing tracker state before switching assets. */
    zmt_sound_off();

    if (game_cards_sfx_mode) {
        music_playing = 0;
        return;
    }
    if ((uint8_t)music_track.title[0] != ZMT_INDEX_GAME) {
        if (load_zmt(&music_track, ZMT_INDEX_GAME) != ERR_SUCCESS) {
            music_playing = 0;
            return;
        }
    }
    zmt_track_reset(&music_track, 1);
    music_playing = 1;
}

void tick_current_music(void)
{
    if (music_playing) {
        /* Active tracker playback (splash or gameplay) uses arrangement flow. */
        zmt_tick(&music_track, 1);
    }
}

void stop_current_music(void)
{
    zmt_sound_off();
    music_playing = 0;
}

static void apply_game_audio_mode(void)
{
    if (game_cards_sfx_mode) {
        /* Card-SFX-only mode: silence gameplay music. */
        if (music_playing && ((uint8_t)music_track.title[0] == ZMT_INDEX_GAME)) {
            stop_current_music();
        }
        /*
         * After tracker shutdown, restore sound/tracker runtime state so
         * plain sound_play() effects remain audible in SFX-only mode.
         */
        zmt_reset(VOL_50);
    } else {
        /* Music-only mode: ensure gameplay track is active outside splash. */
        if (state != STATE_BET || !pending_bankrupt_reset) {
            start_game_music();
        }
    }
}

void audio_toggle_game_audio_mode(void)
{
    game_cards_sfx_mode ^= 1;
    apply_game_audio_mode();
}

void play_card_place_sound(void)
{
    if (game_cards_sfx_mode == 0) {
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
}
