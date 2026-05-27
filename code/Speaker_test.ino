#include <Arduino.h>
#include "driver/i2s.h"
#include <math.h>
#include "esp_idf_version.h"

// MAX98357A pins
const int I2S_DOUT = 18;  // DIN
const int I2S_BCLK = 17;  // BCLK / BCK / SCK
const int I2S_LRC  = 16;  // LRC / WS / LRCLK

const i2s_port_t I2S_PORT = I2S_NUM_0;
const int SAMPLE_RATE = 22050;

bool soundReady = false;

void setupI2S() {
  i2s_config_t i2sConfig = {};
  i2sConfig.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
  i2sConfig.sample_rate = SAMPLE_RATE;
  i2sConfig.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
  i2sConfig.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
  i2sConfig.communication_format = I2S_COMM_FORMAT_STAND_I2S;
  i2sConfig.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
  i2sConfig.dma_buf_count = 8;
  i2sConfig.dma_buf_len = 128;
  i2sConfig.use_apll = false;
  i2sConfig.tx_desc_auto_clear = true;
  i2sConfig.fixed_mclk = 0;

  esp_err_t result = i2s_driver_install(I2S_PORT, &i2sConfig, 0, NULL);

  if (result != ESP_OK) {
    Serial.print("I2S driver install failed: ");
    Serial.println(result);
    soundReady = false;
    return;
  }

  i2s_pin_config_t pinConfig = {};

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(4, 4, 0)
  pinConfig.mck_io_num = I2S_PIN_NO_CHANGE;
#endif

  pinConfig.bck_io_num = I2S_BCLK;
  pinConfig.ws_io_num = I2S_LRC;
  pinConfig.data_out_num = I2S_DOUT;
  pinConfig.data_in_num = I2S_PIN_NO_CHANGE;

  result = i2s_set_pin(I2S_PORT, &pinConfig);

  if (result != ESP_OK) {
    Serial.print("I2S pin setup failed: ");
    Serial.println(result);
    soundReady = false;
    return;
  }

  i2s_zero_dma_buffer(I2S_PORT);
  soundReady = true;
  Serial.println("I2S sound ready.");
}

void playTone(float frequency, int durationMs, float volume) {
  if (!soundReady) {
    Serial.println("Sound not ready.");
    return;
  }

  volume = constrain(volume, 0.0, 0.40);

  const int framesPerChunk = 128;
  int16_t samples[framesPerChunk * 2];

  float phase = 0.0;
  float phaseIncrement = 2.0 * PI * frequency / SAMPLE_RATE;

  int totalFrames = (SAMPLE_RATE * durationMs) / 1000;
  int framesWritten = 0;

  while (framesWritten < totalFrames) {
    int framesNow = min(framesPerChunk, totalFrames - framesWritten);

    for (int i = 0; i < framesNow; i++) {
      float progress = (float)(framesWritten + i) / (float)totalFrames;
      float envelope = 1.0;

      // fade in / fade out，避免爆音
      if (progress < 0.08) {
        envelope = progress / 0.08;
      } else if (progress > 0.92) {
        envelope = (1.0 - progress) / 0.08;
      }

      if (envelope < 0.0) envelope = 0.0;

      int16_t sample = (int16_t)(sin(phase) * 32767.0 * volume * envelope);

      samples[i * 2] = sample;
      samples[i * 2 + 1] = sample;

      phase += phaseIncrement;
      if (phase > 2.0 * PI) {
        phase -= 2.0 * PI;
      }
    }

    size_t bytesWritten = 0;
    i2s_write(
      I2S_PORT,
      (const char *)samples,
      framesNow * 2 * sizeof(int16_t),
      &bytesWritten,
      portMAX_DELAY
    );

    framesWritten += framesNow;
  }

  int16_t silence[framesPerChunk * 2] = {0};
  size_t bytesWritten = 0;
  i2s_write(I2S_PORT, (const char *)silence, sizeof(silence), &bytesWritten, portMAX_DELAY);
}

void quickBeep() {
  playTone(660.0, 120, 0.32);
  delay(100);
  playTone(880.0, 120, 0.30);
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("MAX98357A quick beep test");
  Serial.println("VIN -> 3V3");
  Serial.println("GND -> GND");
  Serial.println("DIN -> GPIO18");
  Serial.println("BCLK -> GPIO17");
  Serial.println("LRC -> GPIO16");
  Serial.println("SD -> 3V3");
  Serial.println("Speaker -> green terminal only");

  setupI2S();
}

void loop() {
  Serial.println("Beep beep");
  quickBeep();

  // 间隔 0.8 秒再响一次
  delay(800);
}