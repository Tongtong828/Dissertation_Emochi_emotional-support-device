#pragma once

#include <Arduino.h>
#include <FS.h>

/*
  AudioPlayer
  -----------
  Plays short 16-bit PCM WAV files from LittleFS through MAX98357A over I2S.

  Recommended WAV format:
  - WAV
  - 16-bit PCM
  - Mono
  - 22050 Hz

  File paths must start with "/", for example:
  audio.play("/wake.wav");
*/

class AudioPlayer {
public:
  AudioPlayer(int dataOutPin, int bitClockPin, int wordSelectPin, int defaultSampleRate = 22050);

  bool begin();
  bool play(const char *path);
  bool isReady() const;

private:
  int _dataOutPin;
  int _bitClockPin;
  int _wordSelectPin;
  int _defaultSampleRate;
  bool _ready;

  bool setupI2S(int sampleRate);
  bool parseWavHeader(
    File &file,
    uint16_t &channels,
    uint32_t &sampleRate,
    uint16_t &bitsPerSample,
    uint32_t &dataSize,
    uint32_t &dataStart
  );

  void writeSilence(int durationMs);
};
