#include "AudioPlayer.h"

#include <Arduino.h>
#include <LittleFS.h>
#include "driver/i2s.h"
#include "esp_idf_version.h"

#ifndef I2S_COMM_FORMAT_STAND_I2S
  #define I2S_COMM_FORMAT_STAND_I2S I2S_COMM_FORMAT_I2S
#endif

static const i2s_port_t AUDIO_I2S_PORT = I2S_NUM_0;

AudioPlayer::AudioPlayer(int dataOutPin, int bitClockPin, int wordSelectPin, int defaultSampleRate) {
  _dataOutPin = dataOutPin;
  _bitClockPin = bitClockPin;
  _wordSelectPin = wordSelectPin;
  _defaultSampleRate = defaultSampleRate;
  _ready = false;
}

bool AudioPlayer::begin() {
  Serial.println("AudioPlayer: mounting LittleFS...");

  if (!LittleFS.begin(false)) {
    Serial.println("AudioPlayer error: LittleFS mount failed.");
    Serial.println("Please upload the data folder to ESP32 LittleFS first.");
    _ready = false;
    return false;
  }

  Serial.println("AudioPlayer: LittleFS mounted.");

  if (!setupI2S(_defaultSampleRate)) {
    Serial.println("AudioPlayer error: I2S setup failed.");
    _ready = false;
    return false;
  }

  _ready = true;
  Serial.println("AudioPlayer: ready.");
  return true;
}

bool AudioPlayer::isReady() const {
  return _ready;
}

bool AudioPlayer::setupI2S(int sampleRate) {
  i2s_config_t i2sConfig = {};
  i2sConfig.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
  i2sConfig.sample_rate = sampleRate;
  i2sConfig.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
  i2sConfig.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
  i2sConfig.communication_format = I2S_COMM_FORMAT_STAND_I2S;
  i2sConfig.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
  i2sConfig.dma_buf_count = 8;
  i2sConfig.dma_buf_len = 128;
  i2sConfig.use_apll = false;
  i2sConfig.tx_desc_auto_clear = true;
  i2sConfig.fixed_mclk = 0;

  esp_err_t result = i2s_driver_install(AUDIO_I2S_PORT, &i2sConfig, 0, NULL);

  if (result != ESP_OK) {
    Serial.print("AudioPlayer error: i2s_driver_install failed: ");
    Serial.println(result);
    return false;
  }

  i2s_pin_config_t pinConfig = {};

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(4, 4, 0)
  pinConfig.mck_io_num = I2S_PIN_NO_CHANGE;
#endif

  pinConfig.bck_io_num = _bitClockPin;
  pinConfig.ws_io_num = _wordSelectPin;
  pinConfig.data_out_num = _dataOutPin;
  pinConfig.data_in_num = I2S_PIN_NO_CHANGE;

  result = i2s_set_pin(AUDIO_I2S_PORT, &pinConfig);

  if (result != ESP_OK) {
    Serial.print("AudioPlayer error: i2s_set_pin failed: ");
    Serial.println(result);
    return false;
  }

  i2s_zero_dma_buffer(AUDIO_I2S_PORT);
  return true;
}

static uint32_t readUInt32LE(File &file) {
  uint8_t b[4];
  if (file.read(b, 4) != 4) return 0;
  return ((uint32_t)b[0]) |
         ((uint32_t)b[1] << 8) |
         ((uint32_t)b[2] << 16) |
         ((uint32_t)b[3] << 24);
}

static uint16_t readUInt16LE(File &file) {
  uint8_t b[2];
  if (file.read(b, 2) != 2) return 0;
  return ((uint16_t)b[0]) | ((uint16_t)b[1] << 8);
}

bool AudioPlayer::parseWavHeader(
  File &file,
  uint16_t &channels,
  uint32_t &sampleRate,
  uint16_t &bitsPerSample,
  uint32_t &dataSize,
  uint32_t &dataStart
) {
  channels = 0;
  sampleRate = 0;
  bitsPerSample = 0;
  dataSize = 0;
  dataStart = 0;

  char riff[4];
  if (file.read((uint8_t *)riff, 4) != 4) return false;

  if (memcmp(riff, "RIFF", 4) != 0) {
    Serial.println("AudioPlayer error: not a RIFF file.");
    return false;
  }

  // Skip RIFF chunk size
  readUInt32LE(file);

  char wave[4];
  if (file.read((uint8_t *)wave, 4) != 4) return false;

  if (memcmp(wave, "WAVE", 4) != 0) {
    Serial.println("AudioPlayer error: not a WAVE file.");
    return false;
  }

  bool foundFmt = false;
  bool foundData = false;
  uint16_t audioFormat = 0;

  while (file.available()) {
    char chunkId[4];

    if (file.read((uint8_t *)chunkId, 4) != 4) {
      break;
    }

    uint32_t chunkSize = readUInt32LE(file);
    uint32_t chunkDataStart = file.position();

    if (memcmp(chunkId, "fmt ", 4) == 0) {
      audioFormat = readUInt16LE(file);
      channels = readUInt16LE(file);
      sampleRate = readUInt32LE(file);

      // Skip byteRate and blockAlign
      readUInt32LE(file);
      readUInt16LE(file);

      bitsPerSample = readUInt16LE(file);
      foundFmt = true;

    } else if (memcmp(chunkId, "data", 4) == 0) {
      dataSize = chunkSize;
      dataStart = file.position();
      foundData = true;
      break;
    }

    // Move to next chunk, including padding byte if chunk size is odd.
    uint32_t nextChunk = chunkDataStart + chunkSize;
    if (chunkSize % 2 == 1) {
      nextChunk++;
    }

    file.seek(nextChunk);
  }

  if (!foundFmt || !foundData) {
    Serial.println("AudioPlayer error: WAV missing fmt or data chunk.");
    return false;
  }

  if (audioFormat != 1) {
    Serial.println("AudioPlayer error: WAV is not PCM.");
    return false;
  }

  if (bitsPerSample != 16) {
    Serial.println("AudioPlayer error: WAV must be 16-bit PCM.");
    return false;
  }

  if (channels != 1 && channels != 2) {
    Serial.println("AudioPlayer error: WAV must be mono or stereo.");
    return false;
  }

  return true;
}

bool AudioPlayer::play(const char *path) {
  if (!_ready) {
    Serial.println("AudioPlayer warning: not ready, cannot play.");
    return false;
  }

  File file = LittleFS.open(path, "r");

  if (!file) {
    Serial.print("AudioPlayer error: file not found: ");
    Serial.println(path);
    return false;
  }

  uint16_t channels = 0;
  uint32_t sampleRate = 0;
  uint16_t bitsPerSample = 0;
  uint32_t dataSize = 0;
  uint32_t dataStart = 0;

  if (!parseWavHeader(file, channels, sampleRate, bitsPerSample, dataSize, dataStart)) {
    file.close();
    return false;
  }

  Serial.print("AudioPlayer playing: ");
  Serial.print(path);
  Serial.print(" | ");
  Serial.print(sampleRate);
  Serial.print(" Hz | ");
  Serial.print(channels);
  Serial.print(" ch | ");
  Serial.print(bitsPerSample);
  Serial.println(" bit");

  i2s_set_sample_rates(AUDIO_I2S_PORT, sampleRate);

  file.seek(dataStart);

  uint32_t remaining = dataSize;

  const size_t INPUT_BUFFER_SIZE = 512;
  uint8_t inputBuffer[INPUT_BUFFER_SIZE];

  // Stereo output buffer for mono input.
  int16_t stereoBuffer[INPUT_BUFFER_SIZE];

  while (remaining > 0) {
    size_t bytesToRead = remaining > INPUT_BUFFER_SIZE ? INPUT_BUFFER_SIZE : remaining;

    if (channels == 2) {
      // Keep stereo buffer aligned to complete 16-bit stereo frames.
      bytesToRead -= bytesToRead % 4;
    } else {
      // Keep mono buffer aligned to complete 16-bit samples.
      bytesToRead -= bytesToRead % 2;
    }

    if (bytesToRead == 0) {
      break;
    }

    size_t bytesRead = file.read(inputBuffer, bytesToRead);

    if (bytesRead == 0) {
      break;
    }

    if (channels == 1) {
      int16_t *monoSamples = (int16_t *)inputBuffer;
      int sampleCount = bytesRead / 2;

      for (int i = 0; i < sampleCount; i++) {
        stereoBuffer[i * 2] = monoSamples[i];
        stereoBuffer[i * 2 + 1] = monoSamples[i];
      }

      size_t bytesWritten = 0;
      i2s_write(
        AUDIO_I2S_PORT,
        (const char *)stereoBuffer,
        sampleCount * 2 * sizeof(int16_t),
        &bytesWritten,
        portMAX_DELAY
      );

    } else {
      size_t bytesWritten = 0;
      i2s_write(
        AUDIO_I2S_PORT,
        (const char *)inputBuffer,
        bytesRead,
        &bytesWritten,
        portMAX_DELAY
      );
    }

    remaining -= bytesRead;
  }

  file.close();
  writeSilence(50);
  return true;
}

void AudioPlayer::writeSilence(int durationMs) {
  const int sampleRate = _defaultSampleRate;
  const int frames = (sampleRate * durationMs) / 1000;

  const int framesPerChunk = 128;
  int16_t silence[framesPerChunk * 2] = {0};

  int framesWrittenTotal = 0;

  while (framesWrittenTotal < frames) {
    int framesNow = frames - framesWrittenTotal;

    if (framesNow > framesPerChunk) {
      framesNow = framesPerChunk;
    }

    size_t bytesWritten = 0;
    i2s_write(
      AUDIO_I2S_PORT,
      (const char *)silence,
      framesNow * 2 * sizeof(int16_t),
      &bytesWritten,
      portMAX_DELAY
    );

    framesWrittenTotal += framesNow;
  }
}
