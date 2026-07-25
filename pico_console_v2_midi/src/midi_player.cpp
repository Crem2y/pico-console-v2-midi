// headers
#include <stdint.h>
#include "common.h"
#include "v2_midi.hpp"

extern audioSystem Audio;

#define MIDI_CHANNEL_COUNT 16
#define MIDI_VOICE_COUNT 64
#define MIDI_MAX_TRACKS 32

typedef struct {
    uint8_t program;
    uint8_t volume;
    uint8_t expression;
    uint8_t pan;
    uint16_t pitch_bend;
} midi_channel_state_t;

typedef struct {
    bool active;
    uint8_t midi_ch;
    uint8_t note;
    uint32_t age;
} midi_voice_state_t;

typedef struct {
    const uint8_t* data;
    size_t length;
    size_t pos;
    uint8_t running_status;
    uint32_t next_tick;
    bool ended;
} midi_track_state_t;

static midi_channel_state_t midi_channels[MIDI_CHANNEL_COUNT];
static midi_voice_state_t midi_voices[MIDI_VOICE_COUNT];

static uint32_t midi_voice_age = 0;

static uint16_t midi_format;
static uint16_t midi_track_count;
static uint16_t midi_division;
static midi_track_state_t tracks[MIDI_MAX_TRACKS] = {};
static uint32_t tempo_us = 500000;
static uint32_t current_tick = 0;

static uint64_t midi_current_time_us = 0;
static time_us_t midi_wait_start_us = 0;
static uint64_t midi_wait_duration_us = 0;
static bool midi_waiting = false;

uint8_t play_status = 0;

static uint16_t read_be16(const uint8_t* p) {
    return ((uint16_t)p[0] << 8) | p[1];
}

static uint32_t read_be32(const uint8_t* p) {
    return ((uint32_t)p[0] << 24) |
           ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) |
           p[3];
}

static bool read_vlq(const uint8_t* data, size_t length, size_t* pos, uint32_t* value) {
    uint32_t result = 0;

    for (int i = 0; i < 4; i++) {
        if (*pos >= length) {
            return false;
        }

        uint8_t b = data[(*pos)++];
        result = (result << 7) | (b & 0x7F);

        if (!(b & 0x80)) {
            *value = result;
            return true;
        }
    }

    return false;
}

static void midi_reset_state(void) {
    midi_voice_age = 0;

    for (uint8_t ch = 0; ch < MIDI_CHANNEL_COUNT; ch++) {
        midi_channels[ch].program = 0;
        midi_channels[ch].volume = 127;
        midi_channels[ch].expression = 127;
        midi_channels[ch].pan = 64;
        midi_channels[ch].pitch_bend = 8192;
    }

    for (uint8_t voice = 0; voice < MIDI_VOICE_COUNT; voice++) {
        midi_voices[voice].active = false;
        midi_voices[voice].midi_ch = 0;
        midi_voices[voice].note = 0;
        midi_voices[voice].age = 0;
    }
}

static wave_t midi_program_to_wave(uint8_t program) {
    static const wave_t wave_table[16] = {
        WAVE_TRIANGLE,   // 0~7: Piano
        WAVE_SINE,       // 8~15: Chromatic Percussion
        WAVE_SQUARE_50,  // 16~23: Organ
        WAVE_SAWTOOTH,   // 24~31: Guitar
        WAVE_SQUARE_25,  // 32~39: Bass
        WAVE_SAWTOOTH,   // 40~47: Strings
        WAVE_SAWTOOTH,   // 48~55: Ensemble
        WAVE_SQUARE_25,  // 56~63: Brass
        WAVE_SQUARE_50,  // 64~71: Reed
        WAVE_SINE,       // 72~79: Pipe
        WAVE_SQUARE_50,  // 80~87: Synth Lead
        WAVE_TRIANGLE,   // 88~95: Synth Pad
        WAVE_SINE,       // 96~103: Synth Effects
        WAVE_SAWTOOTH,   // 104~111: Ethnic
        WAVE_NOISE,      // 112~119: Percussive
        WAVE_NOISE       // 120~127: Sound Effects
    };

    return wave_table[program >> 3];
}

static void midi_apply_pan(uint8_t voice, uint8_t pan) {
    uint8_t left;
    uint8_t right;

    if (pan <= 64) {
        left = 255;
        right = (uint16_t)pan * 255 / 64;
    } else {
        left = (uint16_t)(127 - pan) * 255 / 63;
        right = 255;
    }

    Audio.set_mix(voice, left, right);
}

static uint8_t midi_calculate_volume(uint8_t midi_ch, uint8_t velocity) {
    uint32_t volume = (uint32_t)velocity *
                      midi_channels[midi_ch].volume *
                      midi_channels[midi_ch].expression;

    return volume * 127 / (127 * 127 * 127);
}

static uint8_t midi_allocate_voice(void) {
    for (uint8_t voice = 0; voice < MIDI_VOICE_COUNT; voice++) {
        if (!midi_voices[voice].active) {
            return voice;
        }
    }

    uint8_t oldest_voice = 0;
    uint32_t oldest_age = midi_voices[0].age;

    for (uint8_t voice = 1; voice < MIDI_VOICE_COUNT; voice++) {
        if (midi_voices[voice].age < oldest_age) {
            oldest_voice = voice;
            oldest_age = midi_voices[voice].age;
        }
    }

    return oldest_voice;
}

static void play_midi_drum(uint8_t note, uint8_t velocity) {
    uint8_t play_note;
    wave_t wave;

    switch (note) {
        case 35:
        case 36:
            play_note = 24;
            wave = WAVE_TRIANGLE;
            break;

        case 38:
        case 40:
            play_note = 36;
            wave = WAVE_NOISE;
            break;

        case 41:
        case 43:
        case 45:
        case 47:
        case 48:
        case 50:
            play_note = 55 + (note - 41);
            wave = WAVE_TRIANGLE;
            break;

        case 42:
        case 44:
        case 46:
        case 49:
        case 51:
            play_note = 42;
            wave = WAVE_NOISE;
            break;

        default:
            play_note = 36;
            wave = WAVE_NOISE;
            break;
    }

    uint8_t voice = midi_allocate_voice();

    if (midi_voices[voice].active) {
        Audio.stop_note(voice);
    }

    Audio.set_wave(voice, wave);
    Audio.set_mix(voice, 255, 255);
    Audio.play_note_num(voice, play_note, velocity / 2);

    midi_voices[voice].active = true;
    midi_voices[voice].midi_ch = 9;
    midi_voices[voice].note = note;
    midi_voices[voice].age = midi_voice_age++;
}

static void play_midi_note(uint8_t midi_ch, uint8_t note, uint8_t velocity) {
    if (midi_ch >= MIDI_CHANNEL_COUNT || note >= 128 || velocity == 0) {
        return;
    }

    uint8_t voice = midi_allocate_voice();

    if (midi_voices[voice].active) {
        Audio.stop_note(voice);
    }

    Audio.set_wave(voice, midi_program_to_wave(midi_channels[midi_ch].program));
    midi_apply_pan(voice, midi_channels[midi_ch].pan);
    Audio.play_note_num(voice, note, midi_calculate_volume(midi_ch, velocity));

    midi_voices[voice].active = true;
    midi_voices[voice].midi_ch = midi_ch;
    midi_voices[voice].note = note;
    midi_voices[voice].age = midi_voice_age++;
}

static void stop_midi_note(uint8_t midi_ch, uint8_t note) {
    for (uint8_t voice = 0; voice < MIDI_VOICE_COUNT; voice++) {
        if (midi_voices[voice].active &&
            midi_voices[voice].midi_ch == midi_ch &&
            midi_voices[voice].note == note) {

            Audio.stop_note(voice);
            midi_voices[voice].active = false;
            return;
        }
    }
}

static void stop_midi_channel(uint8_t midi_ch) {
    for (uint8_t voice = 0; voice < MIDI_VOICE_COUNT; voice++) {
        if (midi_voices[voice].active && midi_voices[voice].midi_ch == midi_ch) {
            Audio.stop_note(voice);
            midi_voices[voice].active = false;
        }
    }
}

static void stop_all_midi_notes(void) {
    for (uint8_t voice = 0; voice < MIDI_VOICE_COUNT; voice++) {
        if (midi_voices[voice].active) {
            Audio.stop_note(voice);
            midi_voices[voice].active = false;
        }
    }
}

static void midi_update_channel_pan(uint8_t midi_ch) {
    for (uint8_t voice = 0; voice < MIDI_VOICE_COUNT; voice++) {
        if (midi_voices[voice].active && midi_voices[voice].midi_ch == midi_ch) {
            midi_apply_pan(voice, midi_channels[midi_ch].pan);
        }
    }
}

static bool midi_track_read_next_delta(midi_track_state_t* track) {
    if (track->ended || track->pos >= track->length) {
        track->ended = true;
        return false;
    }

    uint32_t delta;

    if (!read_vlq(track->data, track->length, &track->pos, &delta)) {
        track->ended = true;
        return false;
    }

    track->next_tick += delta;
    return true;
}

static void midi_process_channel_event(midi_track_state_t* track, uint8_t status) {
    uint8_t cmd = status & 0xF0;
    uint8_t midi_ch = status & 0x0F;
    uint8_t data_length = (cmd == 0xC0 || cmd == 0xD0) ? 1 : 2;

    if (track->pos + data_length > track->length) {
        track->ended = true;
        return;
    }

    uint8_t data1 = track->data[track->pos++];
    uint8_t data2 = 0;

    if (data_length == 2) {
        data2 = track->data[track->pos++];
    }

    LOGT("cmd = 0x%02X, ch = %u", cmd, midi_ch);

    switch (cmd) {
        case 0x80:
            LOGT(", note off = %u, velocity = %u\n", data1, data2);
            stop_midi_note(midi_ch, data1);
            break;

        case 0x90:
            if (data2 == 0) {
                LOGT(", note off = %u\n", data1);
                stop_midi_note(midi_ch, data1);
            } else if (midi_ch == 9) {
                LOGT(", note on = %u, velocity = %u\n", data1, data2);
                play_midi_drum(data1, data2);
            } else {
                LOGT(", note on = %u, velocity = %u\n", data1, data2);
                play_midi_note(midi_ch, data1, data2);
            }
            break;

        case 0xA0:
            LOGT(", poly pressure note = %u, pressure = %u\n", data1, data2);
            break;

        case 0xB0:
            LOGT(", control = %u, value = %u\n", data1, data2);

            switch (data1) {
                case 7:
                    midi_channels[midi_ch].volume = data2;
                    break;

                case 10:
                    midi_channels[midi_ch].pan = data2;
                    midi_update_channel_pan(midi_ch);
                    break;

                case 11:
                    midi_channels[midi_ch].expression = data2;
                    break;

                case 120:
                case 123:
                    stop_midi_channel(midi_ch);
                    break;

                case 121:
                    midi_channels[midi_ch].volume = 127;
                    midi_channels[midi_ch].expression = 127;
                    midi_channels[midi_ch].pan = 64;
                    midi_channels[midi_ch].pitch_bend = 8192;
                    midi_update_channel_pan(midi_ch);
                    break;

                default:
                    break;
            }
            break;

        case 0xC0:
            LOGT(", program = %u\n", data1);
            midi_channels[midi_ch].program = data1;
            break;

        case 0xD0:
            LOGT(", channel pressure = %u\n", data1);
            break;

        case 0xE0:
            midi_channels[midi_ch].pitch_bend = ((uint16_t)data2 << 7) | data1;
            LOGT(", pitch bend = %u\n", midi_channels[midi_ch].pitch_bend);
            break;

        default:
            LOGT(", data1 = %u, data2 = %u\n", data1, data2);
            break;
    }
}

static void midi_process_meta_event(midi_track_state_t* track, uint32_t* tempo_us) {
    track->running_status = 0;

    if (track->pos >= track->length) {
        track->ended = true;
        return;
    }

    uint8_t meta_type = track->data[track->pos++];
    uint32_t meta_length;

    if (!read_vlq(track->data, track->length, &track->pos, &meta_length)) {
        track->ended = true;
        return;
    }

    if (track->pos + meta_length > track->length) {
        track->ended = true;
        return;
    }

    if (meta_type == 0x51 && meta_length == 3) {
        uint32_t new_tempo = ((uint32_t)track->data[track->pos] << 16) |
                             ((uint32_t)track->data[track->pos + 1] << 8) |
                             track->data[track->pos + 2];

        if (new_tempo != 0) {
            *tempo_us = new_tempo;
            LOGT("tempo = %u us, bpm = %u\n", *tempo_us, 60000000 / *tempo_us);
        }
    } else if (meta_type == 0x2F) {
        LOGT("end of track\n");
        track->pos += meta_length;
        track->ended = true;
        return;
    } else {
        LOGT("meta = 0x%02X, length = %u\n", meta_type, meta_length);
    }

    track->pos += meta_length;
}

static void midi_process_sysex_event(midi_track_state_t* track) {
    track->running_status = 0;

    uint32_t sysex_length;

    if (!read_vlq(track->data, track->length, &track->pos, &sysex_length)) {
        track->ended = true;
        return;
    }

    if (track->pos + sysex_length > track->length) {
        track->ended = true;
        return;
    }

    LOGT("sysex length = %u\n", sysex_length);

    track->pos += sysex_length;
}

static void midi_process_track_event(midi_track_state_t* track, uint32_t* tempo_us) {
    if (track->ended || track->pos >= track->length) {
        track->ended = true;
        return;
    }

    uint8_t status;

    if (track->data[track->pos] & 0x80) {
        status = track->data[track->pos++];

        if (status < 0xF0) {
            track->running_status = status;
        }
    } else {
        if (track->running_status == 0) {
            LOGW("Wrong running status\n");
            track->ended = true;
            return;
        }

        status = track->running_status;
    }

    if (status < 0xF0) {
        midi_process_channel_event(track, status);
    } else if (status == 0xFF) {
        midi_process_meta_event(track, tempo_us);
    } else if (status == 0xF0 || status == 0xF7) {
        midi_process_sysex_event(track);
    } else {
        LOGW("Unknown status: 0x%02X\n", status);
        track->ended = true;
    }

    if (!track->ended && !midi_track_read_next_delta(track)) {
        track->ended = true;
    }
}

void setup_midi(const uint8_t* m) {
    uartLog_set_level(LOG_TRACE);

    midi_reset_state();

    for (uint8_t voice = 0; voice < MIDI_VOICE_COUNT; voice++) {
        Audio.set_vol_env(voice, 25000, 1);
        Audio.set_mix(voice, 255, 255);
        Audio.set_wave(voice, WAVE_SQUARE_50);
        sleep_ms(1);
    }

    if (m[0] != 'M' || m[1] != 'T' || m[2] != 'h' || m[3] != 'd') {
        LOGE("Not a midi file\n");
        return;
    }

    m += 4;

    uint32_t midi_header_length = read_be32(m);
    m += 4;

    if (midi_header_length < 6) {
        LOGE("Wrong MIDI header length: %u\n", midi_header_length);
        return;
    }

    const uint8_t* midi_header = m;

    midi_format = read_be16(midi_header);
    midi_track_count = read_be16(midi_header + 2);
    midi_division = read_be16(midi_header + 4);

    const uint8_t* next_midi_track = midi_header + midi_header_length;

    LOGI("header_length = %u\n", midi_header_length);
    LOGI("format = %u\n", midi_format);
    LOGI("tracks = %u\n", midi_track_count);
    LOGI("division = %u\n", midi_division);

    if (midi_format > 1) {
        LOGE("Unsupported MIDI format: %u\n", midi_format);
        return;
    }

    if (midi_format == 0 && midi_track_count != 1) {
        LOGW("Format 0 MIDI has %u tracks\n", midi_track_count);
    }

    if (midi_track_count == 0 || midi_track_count > MIDI_MAX_TRACKS) {
        LOGE("Unsupported MIDI track count: %u\n", midi_track_count);
        return;
    }

    if (midi_division == 0) {
        LOGE("Wrong MIDI division\n");
        return;
    }

    if (midi_division & 0x8000) {
        LOGE("SMPTE MIDI division is not supported\n");
        return;
    }

    for (uint16_t i = 0; i < midi_track_count; i++) {
        const uint8_t* midi_track = next_midi_track;

        if (midi_track[0] != 'M' || midi_track[1] != 'T' || midi_track[2] != 'r' || midi_track[3] != 'k') {
            LOGE("Wrong MIDI track %02X%02X%02X%02X\n", midi_track[0], midi_track[1], midi_track[2], midi_track[3]);
            return;
        }

        uint32_t midi_track_length = read_be32(midi_track + 4);

        tracks[i].data = midi_track + 8;
        tracks[i].length = midi_track_length;
        tracks[i].pos = 0;
        tracks[i].running_status = 0;
        tracks[i].next_tick = 0;
        tracks[i].ended = false;

        next_midi_track = tracks[i].data + midi_track_length;

        if (!midi_track_read_next_delta(&tracks[i])) {
            tracks[i].ended = true;
        }

        LOGI("track %u length = %u\n", i, midi_track_length);
    }

    midi_current_time_us = 0;
    midi_wait_start_us = get_system_time_us();
    midi_wait_duration_us = 0;
    midi_waiting = false;
    tempo_us = 500000;
    current_tick = 0;
    play_status = 1;
}

void play_midi(void) {
    if (!play_status) {
        return;
    }

    uint16_t processed_tick_count = 0;

    while (processed_tick_count < 32) {
        uint32_t next_tick = UINT32_MAX;
        bool has_active_track = false;

        for (uint16_t i = 0; i < midi_track_count; i++) {
            if (!tracks[i].ended) {
                has_active_track = true;

                if (tracks[i].next_tick < next_tick) {
                    next_tick = tracks[i].next_tick;
                }
            }
        }

        if (!has_active_track) {
            stop_all_midi_notes();
            midi_waiting = false;
            play_status = 0;
            return;
        }

        if (!midi_waiting) {
            uint32_t delta_tick = next_tick - current_tick;
            uint64_t delay_us = (uint64_t)delta_tick * tempo_us / midi_division;

            midi_current_time_us += delay_us;
            midi_wait_duration_us = (uint64_t)delta_tick * tempo_us / midi_division;
            midi_waiting = true;
        }

        time_us_t now = get_system_time_us();

        if ((uint64_t)system_time_elapsed_us(now, midi_wait_start_us) < midi_wait_duration_us) {
            return;
        }

        midi_wait_start_us += (time_us_t)midi_wait_duration_us;
        midi_wait_duration_us = 0;
        midi_waiting = false;
        current_tick = next_tick;

        for (uint16_t i = 0; i < midi_track_count; i++) {
            while (!tracks[i].ended && tracks[i].next_tick == current_tick) {
                midi_process_track_event(&tracks[i], &tempo_us);
            }
        }

        processed_tick_count++;
    }
}

void stop_midi(void) {
    stop_all_midi_notes();
}

uint32_t get_midi_length_ms(void) {
    midi_track_state_t temp_tracks[MIDI_MAX_TRACKS];
    uint32_t temp_current_tick = 0;
    uint32_t temp_tempo_us = 500000;
    uint64_t total_us = 0;

    memcpy(temp_tracks, tracks, sizeof(midi_track_state_t) * midi_track_count);

//    midi_length_scan = true;

    while (true) {
        uint32_t next_tick = UINT32_MAX;
        bool has_active_track = false;

        for (uint16_t i = 0; i < midi_track_count; i++) {
            if (!temp_tracks[i].ended) {
                has_active_track = true;

                if (temp_tracks[i].next_tick < next_tick) {
                    next_tick = temp_tracks[i].next_tick;
                }
            }
        }

        if (!has_active_track) {
            break;
        }

        if (next_tick > temp_current_tick) {
            uint32_t delta_tick = next_tick - temp_current_tick;
            total_us += (uint64_t)delta_tick * temp_tempo_us / midi_division;
            temp_current_tick = next_tick;
        }

        for (uint16_t i = 0; i < midi_track_count; i++) {
            while (!temp_tracks[i].ended && temp_tracks[i].next_tick == temp_current_tick) {
                midi_process_track_event(&temp_tracks[i], &temp_tempo_us);
            }
        }
    }

//    midi_length_scan = false;

    return (uint32_t)(total_us / 1000ULL);
}

uint32_t get_midi_current_time_ms(void) {
    return (uint32_t)(midi_current_time_us / 1000ULL);
}