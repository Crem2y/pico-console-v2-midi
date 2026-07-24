// headers
#include "common.h"
#include "v2_midi.hpp"

extern audioSystem Audio;

uint16_t read_be16(const uint8_t *p) {
    return ((uint16_t)p[0] << 8) | p[1];
}

uint32_t read_be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
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

int8_t midi_channel_note[64];

void play_midi_note(uint8_t note) {
  static uint8_t steal_ch = 0;

  // fine same note
  for (uint8_t ch = 0; ch < 64; ch++) {
    if (midi_channel_note[ch] == note) {
      Audio.play_note_num(ch, note, 32);
      return;
    }
  }

  // find empty channel
  for (uint8_t ch = 0; ch < 64; ch++) {
    if (midi_channel_note[ch] == -1) {
      midi_channel_note[ch] = note;
      Audio.play_note_num(ch, note, 32);
      return;
    }
  }

  // if there is no empty channel, steal the channel
  //Audio.stop_note(steal_ch);

  midi_channel_note[steal_ch] = note;
  Audio.play_note_num(steal_ch, note, 32);

  steal_ch++;
  if (steal_ch >= 64) {
    steal_ch = 0;
  }
}

void stop_midi_note(uint8_t note) {
  for (uint8_t ch = 0; ch < 64; ch++) {
    if (midi_channel_note[ch] == note) {
      Audio.stop_note(ch);
      midi_channel_note[ch] = -1;
      return;
    }
  }
}

void play_midi(const uint8_t* m) {
  //test
  uartLog_set_level(LOG_TRACE);

  //test
  for(int i=0; i<64; i++) {
    midi_channel_note[i] = -1;
  }

  // manual channel setting
  for(int i=0; i<64; i++) {
    Audio.set_vol_env(i, 25000, 1);
    Audio.set_mix(i, 255, 255);
    Audio.set_wave(i, WAVE_SQUARE_50);
    sleep_ms(1);
  }

  // check "MThd"
  if(m[0] != 0x4D ||
    m[1] != 0x54 ||
    m[2] != 0x68 ||
    m[3] != 0x64) {
    LOGE("Not a midi file");
    return;
  }
  m += 4;

  // midi header (big endian)
  uint32_t midi_header_length = read_be32(m);
  m += 4;
  const uint8_t* next_midi_track = m + midi_header_length;
  uint16_t midi_format = read_be16(m);
  m += 2;
  uint16_t midi_tracks = read_be16(m);
  m += 2;
  uint16_t midi_division = read_be16(m);

  LOGI("header_length = %d\n", midi_header_length);
  LOGI("format = %d\n", midi_format);
  LOGI("tracks = %d\n", midi_tracks);
  LOGI("division = %d\n", midi_division);

  uint32_t tempo_us = 500000; // 120 BPM

  for (size_t i = 0; i < midi_tracks; i++) {
    const uint8_t* midi_track = next_midi_track;

    LOGI("--track %u--\n", i);

    // check "MTrk"
    if (midi_track[0] != 0x4D ||
      midi_track[1] != 0x54 ||
      midi_track[2] != 0x72 ||
      midi_track[3] != 0x6B) {
      LOGE("Wrong midi track %02X%02X%02X%02X", midi_track[0], midi_track[1], midi_track[2], midi_track[3]);
      return;
    }

    midi_track += 4;

    uint32_t midi_track_length = read_be32(midi_track);
    midi_track += 4;

    next_midi_track = midi_track + midi_track_length;

    LOGI("track_length = %u\n\n", midi_track_length);
    
    size_t pos = 0;
    uint8_t running_status = 0;
    uint8_t now_ch = 0; //test

    while (pos < midi_track_length) {
      uint32_t track_delta;

      if (!read_vlq( midi_track, midi_track_length, &pos, &track_delta)) {
        LOGW("Wrong delta\n");
        break;
      }

      if (pos >= midi_track_length) {
        break;
      }

      uint8_t status;

      if (midi_track[pos] & 0x80) {
        status = midi_track[pos++];

        if (status < 0xF0) {
          running_status = status;
        }
      } else {
        // Running Status
        if (running_status == 0) {
          LOGW("Wrong running status\n");
          break;
        }

        status = running_status;
      }

      // Channel midi event
      if (status < 0xF0) {
        uint8_t cmd = status & 0xF0;
        uint8_t ch = status & 0x0F;

        uint8_t data_length =
            (cmd == 0xC0 || cmd == 0xD0) ? 1 : 2;

        if (pos + data_length > midi_track_length) {
          break;
        }

        uint8_t data1 = midi_track[pos++];
        uint8_t data2 = 0;

        if (data_length == 2) {
          data2 = midi_track[pos++];
        }

        LOGT("delta = %u, cmd = 0x%02X, ch = %u", track_delta, cmd, ch);

        // test
        uint32_t delay_us = (uint64_t)track_delta * tempo_us / midi_division;
        if (delay_us > 0) {
          sleep_us(delay_us);
        }

        switch (cmd) {
          case 0x80:
            LOGT(", note off = %u, velocity = %u\n", data1, data2);
            stop_midi_note(data1);
            break;

          case 0x90:
            if (data2 == 0) {
              LOGT(", note off = %u\n", data1);
              stop_midi_note(data1);
            } else {
              LOGT(", note on = %u, velocity = %u\n", data1, data2);
              play_midi_note(data1);
              now_ch++;
              if(now_ch >= 64) now_ch = 0;
            }
            break;

          case 0xC0:
            LOGT(", program = %u\n", data1);
            break;

          default:
            LOGT(", data1 = %u, data2 = %u\n", data1, data2);
            break;
        }

        continue;
      }

      // Meta Event
      if (status == 0xFF) {
        running_status = 0;

        if (pos >= midi_track_length) {
          break;
        }

        uint8_t meta_type = midi_track[pos++];

        uint32_t meta_length;

        if (!read_vlq(midi_track, midi_track_length, &pos, &meta_length)) {
          break;
        }

        if (pos + meta_length > midi_track_length) {
          break;
        }

        if (meta_type == 0x51 && meta_length == 3) {
          tempo_us = ((uint32_t)midi_track[pos] << 16) | ((uint32_t)midi_track[pos + 1] << 8) | midi_track[pos + 2];

          LOGT("delta = %u, tempo = %u us, bpm = %u\n", track_delta, tempo_us, 60000000 / tempo_us);
        } else if (meta_type == 0x2F) {
          LOGT("delta = %u, end of track\n", track_delta);
          break;
        } else {
          LOGT("delta = %u, meta = 0x%02X, length = %u\n", track_delta, meta_type, meta_length);
        }

        pos += meta_length;
        continue;
      }

      // SysEx
      if (status == 0xF0 || status == 0xF7) {
        running_status = 0;

        uint32_t sysex_length;

        if (!read_vlq(midi_track, midi_track_length, &pos, &sysex_length)) {
          break;
        }

        if (pos + sysex_length > midi_track_length) {
          break;
        }

        LOGT("delta = %u, sysex length = %u\n", track_delta, sysex_length);

        pos += sysex_length;
        continue;
      }

      LOGW("Unknown status: 0x%02X\n", status);
      break;
    }
  }
}