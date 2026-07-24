// headers
#include "common.h"
#include "v2_midi.hpp"
#include "v2_hw_def.h"

// hw lib init
ledStatus Led = ledStatus(PIN_LED_WL_1, PIN_LED_WL_2, PIN_LED_WL_3, PIN_LED_WL_4);
ili9488_40 Lcd = ili9488_40(PIN_DP_MOSI, PIN_DP_SCK, PIN_DP_CS, PIN_DP_DC, PIN_DP_RST, PIN_DP_BL);
sdCard Sd = sdCard();
pio_uart_tx_t pio_tx;
pio_uart_rx_t pio_rx;
#if ENABLE_RFBRIDGE
pio_uart_tx_t pio_tx_rf;
pio_uart_rx_t pio_rx_rf;
#endif

// middleware lib init
bridgeProtocol Bridge = bridgeProtocol();
bridgeControl SouthBridge = bridgeControl(&Bridge);
#if ENABLE_RFBRIDGE
bridgeProtocol BridgeRf = bridgeProtocol();
bridgeControl RfBridge = bridgeControl(&BridgeRf);
#endif
power Power = power();
charger Charger = charger();
#if !ENABLE_HW_LED && ENABLE_SW_LED
ledControl LedCtrl = ledControl(&BridgeRf);
#else
ledControl LedCtrl = ledControl(&Led);
#endif
gamepad Gamepad = gamepad();
graphicSystem Graphic = graphicSystem(&Lcd);
audioSystem Audio = audioSystem();
temperature Temperature = temperature();

void core1_entry();
void bridge_cmd_handler(const bridge_msg_t* msg);
#if ENABLE_RFBRIDGE
void bridge_cmd_handler_rf(const bridge_msg_t* msg);
#endif

time_ms_t bridge_timer;
time_ms_t led_timer;
time_ms_t gamepad_timer;
time_ms_t temperature_timer;
time_ms_t audio_timer;
time_ms_t vibration_timer;
time_ms_t touch_timer;
time_ms_t sd_timer;

inline int pio_uart_readable_wrapper(void) {
  return pio_uart_rx_readable(&pio_rx);
}
inline int pio_uart_read_wrapper(uint8_t* data, size_t buf_size) {
  return pio_uart_rx_read(&pio_rx, data, buf_size);
}
inline int pio_uart_writeable_wrapper(void) {
  return pio_uart_tx_writeable(&pio_tx);
}
inline int pio_uart_write_wrapper(const uint8_t* data, size_t data_size) {
  return pio_uart_tx_write(&pio_tx, data, data_size);
}

#if ENABLE_RFBRIDGE
inline int pio_uart_readable_wrapper_rf(void) {
  return pio_uart_rx_readable(&pio_rx_rf);
}
inline int pio_uart_read_wrapper_rf(uint8_t* data, size_t buf_size) {
  return pio_uart_rx_read(&pio_rx_rf, data, buf_size);
}
inline int pio_uart_writeable_wrapper_rf(void) {
  return pio_uart_tx_writeable(&pio_tx_rf);
}
inline int pio_uart_write_wrapper_rf(const uint8_t* data, size_t data_size) {
  return pio_uart_tx_write(&pio_tx_rf, data, data_size);
}
#endif
//////// function ////////

#define MIDI_FILE_SIZE (1 * 1024 * 1024) // 1MB
unsigned char* midi_file = (unsigned char*)PSRAM_BASE + MIDI_FILE_SIZE;
char midi_file_name[512] = "";

void midi_file_selector(void);
void load_midi(const char *path);
void play_midi(const uint8_t* midi_file);

int main() { // uses core 0 to sub core
  // log init
  uartLog_init(HW_LOG_CH, PIN_LOG_TX, PIN_LOG_RX, HW_LOG_BAUD);
  uartLog_print("\n\npico console V2 booting...\n\n");

  // bridge init
  pio_uart_tx_init(&pio_tx, HW_BRIDGE_PIO, PIN_BRIDGE_TX, HW_BRIDGE_BAUD);
  pio_uart_rx_init(&pio_rx, HW_BRIDGE_PIO, PIN_BRIDGE_RX, HW_BRIDGE_BAUD);
  bridge_transport_t transport = {pio_uart_readable_wrapper, pio_uart_read_wrapper, pio_uart_writeable_wrapper, pio_uart_write_wrapper};
  Bridge.set_transport_handler(&transport);
  Bridge.set_cmd_handler(bridge_cmd_handler);
  SouthBridge.init();

#if ENABLE_RFBRIDGE // rf bridge init
  pio_uart_tx_init(&pio_tx_rf, HW_RF_BRIDGE_PIO, PIN_RF_BRIDGE_TX, HW_RF_BRIDGE_BAUD);
  pio_uart_rx_init(&pio_rx_rf, HW_RF_BRIDGE_PIO, PIN_RF_BRIDGE_RX, HW_RF_BRIDGE_BAUD);
  bridge_transport_t transport_rf = {pio_uart_readable_wrapper_rf, pio_uart_read_wrapper_rf, pio_uart_writeable_wrapper_rf, pio_uart_write_wrapper_rf};
  BridgeRf.set_transport_handler(&transport_rf);
  BridgeRf.set_cmd_handler(bridge_cmd_handler_rf);
  RfBridge.init();
#endif
  sleep_ms(100);

  LedCtrl.init();
  led_config_t led_config = {.mode = LED_BLINK_REPEAT, .brightness = 255, .update_interval_ms = 500};
  LedCtrl.set_config(LED_CTRL_BUILT_IN, led_config);
  LedCtrl.update();

  // initalizing hardwares
  Power.init();
  Charger.init();
  led_config = {.mode = LED_ON, .brightness = 255, .update_interval_ms = 20, .breathing_step = 10};
  LedCtrl.set_config(LED_CTRL_1, led_config);
  LedCtrl.set_config(LED_CTRL_2, led_config);
  LedCtrl.set_config(LED_CTRL_3, led_config);
  LedCtrl.set_config(LED_CTRL_4, led_config);
  LedCtrl.update();
  LOGI("LED ok\n");
#if ENABLE_PSRAM
  int32_t ret = psram_init(PIN_PSRAM_CS);
  if(ret < 0) {
    LOGE("PSRAM error : %d", ret);
    while(1);
  }
  LOGI("PSRAM ok\n");
#endif
  Graphic.begin();
  Graphic.fillScreen(LCD_BLACK);
  Graphic.set_bright(750);
  Graphic.setTextColor(LCD_WHITE, LCD_BLACK);
  Graphic.setTextSize(1);
  Graphic.set_font(G_FONT_5X8);
  LOGI("LCD ok\n");
  Graphic.setCursor(0,0);
  Graphic.print("Gamepad init...");
  Gamepad.init();
  Gamepad.set_enable(true, false);
  LOGI("Gamepad ok\n");
  Graphic.setCursor(0,0);
  Graphic.print("TEMP init...");
  Temperature.init();
  LOGI("TEMP ok\n");
  Graphic.setCursor(0,0);
  Graphic.print("SD init...");
  Sd.init();
  LOGI("SD ok\n");
  Graphic.setCursor(0,0);
  Graphic.print("               ");
  LOGI("all HWs ok!\n");
  LOGI("core freq = %ld hz\n", SYS_CLK_KHZ * 1000);
  // hardware initalized

  LOGI("go to main loop\n");
  multicore_launch_core1(core1_entry);
  // multicore_fifo_push_blocking(1);
  // boot sequence end

  while (true) {
    time_ms_t now_time = get_system_time_ms();

    Bridge.process_io();
    Bridge.dispatch_rx();
#if ENABLE_RFBRIDGE
    BridgeRf.process_io();
    BridgeRf.dispatch_rx();
#endif

    if(system_time_elapsed_ms(now_time, bridge_timer) > 1000) {
      bridge_timer = now_time;
      SouthBridge.update();
#if ENABLE_RFBRIDGE
      RfBridge.update();
#endif
    }
    if(system_time_elapsed_ms(now_time, led_timer) > 10) {
      led_timer = now_time;
      LedCtrl.update();
    }
    if(system_time_elapsed_ms(now_time, gamepad_timer) > 10) {
      gamepad_timer = now_time;
      Gamepad.update();
    }
    if(system_time_elapsed_ms(now_time, temperature_timer) > 1000) {
      temperature_timer = now_time;
      Temperature.update();
    }
    if(system_time_elapsed_ms(now_time, audio_timer) > 1) {
      audio_timer = now_time;
      Audio.update();
    }
    if(system_time_elapsed_ms(now_time, sd_timer) > 10) {
      sd_timer = now_time;
      Sd.update();
    }
  }

  return 0;
}

void core1_entry() { // uses core 1 to main core

  // multicore_fifo_pop_blocking(); // wait until boot process is done

  // boot animation
  Graphic.setTextSize(2);
  for(int i=0; i<160; i+=1) {
    Graphic.fillRect(150, i-1, (6*2*15), 1, LCD_BLACK);
    Graphic.setCursor(150,i);
    Graphic.print("PICO CONSOLE V2");
    sleep_ms(10);
  }

  Graphic.setCursor(480-(6*2*9),320-(8*2));
  Graphic.print("by Crem2y");
  Graphic.setTextSize(1);
  Graphic.setCursor(206,200);
  Graphic.print("press START");
  Graphic.setCursor(183,210);
  Graphic.print("or touch the screen");

  Graphic.setCursor(0,0);
  Graphic.print("press L/R to change bright");

  time_ms_t btn_time_ms = 0;
  time_ms_t display_time_ms = 0;
  bool display_text = false;
  bool display_bridge_status = false;
  while(true) {
    time_ms_t now_time = get_system_time_ms();
    if(Gamepad.is_btn_pressed(BTN_START)) break;

    if(system_time_elapsed_ms(now_time, btn_time_ms) > 200) {
      btn_time_ms = now_time;

      uint16_t bright = Graphic.get_bright();
      if(Gamepad.is_btn_pressed(BTN_SL) && bright > 50) {
        Graphic.set_bright(bright - 50);
        Graphic.setCursor(0,8);
        Graphic.printf("bright : %d ", bright - 50);
      }
      if(Gamepad.is_btn_pressed(BTN_SR) && bright < 1000) {
        Graphic.set_bright(bright + 50);
        Graphic.setCursor(0,8);
        Graphic.printf("bright : %d ", bright + 50);
      }

      Graphic.setCursor(480-66,0);
      Graphic.printf("BAT:% 3.1f%%", Charger.get_bat_level());
    }

    if(system_time_elapsed_ms(now_time, display_time_ms) > 1000) {
      display_time_ms = now_time;
      if(display_text) {
        Graphic.fillRect(206,200,(6*11),8,LCD_BLACK);
      } else {
        Graphic.setCursor(206,200);
        Graphic.print("press START");
      }
      display_text = !display_text;
    }

    // to remove flickering
    if(!SouthBridge.connected) {
      if(!display_bridge_status) {
        Graphic.setCursor(162,240);
        Graphic.print("southbridge disconnected!");
        display_bridge_status = true;
      }
    } else {
      if(display_bridge_status) {
        Graphic.fillRect(162,240,(26*6),8,LCD_BLACK);
        display_bridge_status = false;
      }
    }
  }

  LedCtrl.set_mode(LED_CTRL_1, LED_DARKER);
  LedCtrl.set_mode(LED_CTRL_2, LED_DARKER);
  LedCtrl.set_mode(LED_CTRL_3, LED_DARKER);
  LedCtrl.set_mode(LED_CTRL_4, LED_DARKER);

  music_note_t boot_notes[2] = {
    {0, 6, 0, 32},   // C6
    {0, 7, 0, 32}    // C7
  };

  music_table_t boot_music = {
    .len = 2,
    .note_duration_ms = 100,
    .notes = boot_notes
  };

  Audio.set_master_config(127);
  for(int i=0; i<4; i++) {
    Audio.set_vol_env(i, 25000, 1);
  }

  // if SELECT+START, quiet boot
  if(!Gamepad.is_btn_pressed(BTN_SELECT)) {
    Audio.play_music(&boot_music, false);
  }

  while (1) {
    Graphic.fillScreen(LCD_BLACK);
    Graphic.setTextColor(LCD_WHITE, LCD_BLACK);
    midi_file_selector();

    Graphic.fillScreen(LCD_BLACK);
    Graphic.setTextColor(LCD_WHITE, LCD_BLACK);
    Graphic.set_font(G_FONT_16);
    Graphic.setCursor(0,0);
    Graphic.printf("Now playing : %s", midi_file_name);

    play_midi(midi_file);
  }
}

void load_midi(const char *path) {
  FIL fil;
  FRESULT fr = f_open(&fil, path, FA_READ);
  if (FR_OK != fr) {
    Graphic.printf("f_open error: %s (%d)\n", FRESULT_str(fr), fr);
    return;
  }

  uint8_t buf[256];
  UINT bytes_read;
  size_t pos = 0;

  while (1) {
    fr = f_read(&fil, buf, sizeof buf, &bytes_read);
    if (FR_OK != fr) {
      Graphic.printf("f_read error: %s (%d)\n", FRESULT_str(fr), fr);
      break;
    }

    if (bytes_read == 0) {
      break;
    }

    memcpy(&midi_file[pos], buf, bytes_read);
    pos += bytes_read;
    if(pos > MIDI_FILE_SIZE) {
      break;
    }
  }

  fr = f_close(&fil);
  if (FR_OK != fr) {
    Graphic.printf("f_close error: %s (%d)\n", FRESULT_str(fr), fr);
  }
}

void ls_cursor(const char *dir, int cursor, char* cursor_path, uint8_t* cursor_type) {
    char cwdbuf[FF_LFN_BUF] = {0};
    FRESULT fr; /* Return value */
    char const *p_dir;
    if (dir[0]) {
        p_dir = dir;
    } else {
        fr = f_getcwd(cwdbuf, sizeof cwdbuf);
        if (FR_OK != fr) {
            Graphic.printf("f_getcwd error: %s (%d)\n", FRESULT_str(fr), fr);
            return;
        }
        p_dir = cwdbuf;
    }
    LOGI("Directory Listing: %s\n", p_dir);
    DIR dj = {};      /* Directory object */
    FILINFO fno = {}; /* File information */
    assert(p_dir);
    fr = f_findfirst(&dj, &fno, p_dir, "*.mid");
    if (FR_OK != fr) {
        Graphic.printf("f_findfirst error: %s (%d)\n", FRESULT_str(fr), fr);
        return;
    }

    int count = 0;
    uint8_t type = 0;
    *cursor_type = type;

    while (fr == FR_OK && fno.fname[0]) { /* Repeat while an item is found */
        /* Create a string that includes the file name, the file size and the
         attributes string. */
        const char *pcWritableFile = "writable file",
                   *pcReadOnlyFile = "read only file",
                   *pcDirectory = "directory";
        const char *pcAttrib;
        /* Point pcAttrib to a string that describes the file. */
        if (fno.fattrib & AM_DIR) {
            pcAttrib = pcDirectory;
            type = 1;
        } else if (fno.fattrib & AM_RDO) {
            pcAttrib = pcReadOnlyFile;
            type = 2;
        } else {
            pcAttrib = pcWritableFile;
            type = 3;
        }
        /* Create a string that includes the file name, the file size and the
         attributes string. */
        if(count == cursor) {
          strncpy(cursor_path, fno.fname, 512);
          Graphic.set_text_color(LCD_BLACK, LCD_WHITE);
          *cursor_type = type;
        } else {
          Graphic.set_text_color(LCD_WHITE, LCD_BLACK);
        }
        // Graphic.printf("%s [%s] [size=%llu]\n", fno.fname, pcAttrib, fno.fsize);
        Graphic.printf("%s [%s]\n", fno.fname, pcAttrib);

        fr = f_findnext(&dj, &fno); /* Search for next item */
        count++;
    }
    f_closedir(&dj);
}

void midi_file_selector(void) {
  Graphic.setTextSize(2);
  Graphic.setCursor(0,0);
  Graphic.print("Select MIDI");

  Graphic.setCursor(0,16);
  Graphic.print("Loading...");

  enum sd_status status = SD_NO_CARD;
  enum sd_status prev_status = SD_CARD_ERR;

  bool need_display_update = true;

  char path[512] = "";
  char cursor_path[512] = "";
  uint8_t cursor = 0;
  uint8_t cursor_type = 0;
  bool file_reading = false;

    while(1) {
    sleep_ms(100);

    status = Sd.get_status();
    if(prev_status != status) {
      prev_status = status;
      Graphic.setCursor(0,16);
      Graphic.print("SD card : ");
      switch(status) {
        case SD_NO_CARD:
          Graphic.print("not inserted\n");
          strcpy(path, "");
          file_reading = false;
          cursor = 0;
          break;
        case SD_NOT_MOUNTED:
          Graphic.print("not mounted \n");
          break;
        case SD_MOUNTING:
          Graphic.print("mounting... \n");
          break;
        case SD_MOUNTED:
          Graphic.print("mounted     \n");
          break;
        case SD_CARD_ERR:
          Graphic.print("ERROR!!     \n");
          break;
        default:
          break;
      }
      need_display_update = true;
    }

    if(need_display_update) {
      need_display_update = false;
      Graphic.fillRect(0,16*2,480,(320-32),LCD_BLACK);
      Graphic.setCursor(0,16*3);
      if(status == SD_MOUNTED) {
        Graphic.set_font(G_FONT_16);
        FRESULT fr = f_getcwd(path, 512);
        if (FR_OK == fr) {
          if(file_reading) {
            memset(midi_file, 0x00, MIDI_FILE_SIZE);

            // get midi file name
            strncpy(midi_file_name, cursor_path, 512);
            Graphic.printf("Loading MIDI '%s'...", cursor_path);
            load_midi(midi_file_name);
            Graphic.print("ok\n");
            Graphic.set_font(G_FONT_5X8);
            return;
          } else {
            Graphic.printf("list of '%s'\n", path);
            ls_cursor(path, cursor, cursor_path, &cursor_type);
          }
        }
        Graphic.set_font(G_FONT_5X8);
        Graphic.set_text_color(LCD_WHITE, LCD_BLACK);
      }
    }

    if(Gamepad.is_btn_pressed(BTN_A)) {
      if(cursor_type) {
        if(cursor_type == 1) { // directory
          f_chdir(cursor_path);
          cursor = 0;
        } else { // file
          file_reading = true;
        }
        need_display_update = true;
      }
    }
    if(Gamepad.is_btn_pressed(BTN_B)) {
      if(file_reading) {
        file_reading = false;
      } else {
        f_chdir("..");
        cursor = 0;
      }
      need_display_update = true;
    }

    if(Gamepad.is_btn_pressed(BTN_S1_UP) || Gamepad.is_btn_pressed(BTN_UP)) {
      if(cursor > 0) cursor--;
      need_display_update = true;
    }
    if(Gamepad.is_btn_pressed(BTN_S1_DOWN) || Gamepad.is_btn_pressed(BTN_DOWN)) {
      if(cursor < 128) cursor++;
      need_display_update = true;
    }
  }
}

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
  Graphic.setTextColor(LCD_WHITE, LCD_BLACK);
  Graphic.setTextSize(1);
  Graphic.set_font(G_FONT_5X8);
  Graphic.setCursor(0,0);

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
    Graphic.print("Not a midi file");
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
      Graphic.printf("Wrong midi track");
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

      // test
      uint32_t delay_us = (uint64_t)track_delta * tempo_us / midi_division;
      if (delay_us > 0) {
        sleep_us(delay_us);
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

void bridge_cmd_handler(const bridge_msg_t* msg) {
  enum bridge_cmd command = (enum bridge_cmd)msg->cmd;
  SouthBridge.update_last_comm_time();

  switch (command)
  {
  case CMD_HW_INFO_RES:
    SouthBridge.recv_bridge_hw_info_res(msg->payload, msg->payload_size);
    break;
  case CMD_HW_NAME_RES:
    SouthBridge.recv_bridge_hw_name_res(msg->payload, msg->payload_size);
    break;
  case CMD_SW_INFO_RES:
    SouthBridge.recv_bridge_sw_info_res(msg->payload, msg->payload_size);
    break;
  case CMD_TEMPERATURE_DATA:
    Temperature.recv_bridge_data(msg->payload, msg->payload_size);
    break;
  case CMD_POWER_STATUS:
    Power.recv_bridge_power_status(msg->payload, msg->payload_size);
    break;
  case CMD_BATTERY_STATUS:
    Charger.recv_bridge_bat_status(msg->payload, msg->payload_size);
    break;
  case CMD_GAMEPAD_DATA:
    Gamepad.recv_bridge_data(msg->payload, msg->payload_size);
    break;
  case CMD_GAMEPAD_RAW_DATA:
    Gamepad.recv_bridge_raw_data(msg->payload, msg->payload_size);
    break;
  default:
    break;
  }
}

#if ENABLE_RFBRIDGE
void bridge_cmd_handler_rf(const bridge_msg_t* msg) {
  enum bridge_cmd command = (enum bridge_cmd)msg->cmd;
  RfBridge.update_last_comm_time();

  switch (command)
  {
  case CMD_HW_INFO_RES:
    RfBridge.recv_bridge_hw_info_res(msg->payload, msg->payload_size);
    break;
  case CMD_HW_NAME_RES:
    RfBridge.recv_bridge_hw_name_res(msg->payload, msg->payload_size);
    break;
  case CMD_SW_INFO_RES:
    RfBridge.recv_bridge_sw_info_res(msg->payload, msg->payload_size);
    break;
  default:
    break;
  }
}
#endif