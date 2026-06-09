/*
  Emotional Plush Prototype - WAV Audio Version

  Files in this sketch:
  - EmotionalPlush.ino   -> interaction / behaviour logic
  - AudioPlayer.h        -> audio player interface
  - AudioPlayer.cpp      -> WAV playback through MAX98357A + LittleFS

  Required audio files in LittleFS data folder:
  /wake.wav
  /sleep.wav
  /comfort_1.wav
  /comfort_2.wav
  /comfort_3.wav
  /soothe_start.wav
  /calm_done.wav
  /meditation_start.wav
  /meditation_end.wav
*/

#include <Arduino.h>
#include "AudioPlayer.h"

// ===================== PINS =====================

// FSR:
// FSR one side  -> 3V3
// FSR other side -> GPIO1
const int FSR_PIN = 1;

// Piezo module:
// S -> GPIO4
// + -> 3V3
// - -> GND
const int PIEZO_PIN = 4;

// Vibration motor module:
// IN -> GPIO5
// VCC -> 3V3 or 5V/VIN
// GND -> GND
const int MOTOR_PIN = 5;

// MAX98357A:
// DIN  -> GPIO18
// BCLK -> GPIO17
// LRC  -> GPIO16
// VIN  -> 3V3
// GND  -> GND
// SD   -> 3V3
const int I2S_DOUT = 18;
const int I2S_BCLK = 17;
const int I2S_LRC  = 16;

// Audio player object.
// This class handles LittleFS mounting and WAV playback.
AudioPlayer audio(I2S_DOUT, I2S_BCLK, I2S_LRC);

// ===================== BASIC SETTINGS =====================

// If motor does not vibrate, change true to false.
const bool MOTOR_ACTIVE_HIGH = true;

// If head tap does not work, change true to false.
const bool PIEZO_ACTIVE_HIGH = true;

// You currently use FSR without external resistor.
// After adding 10k resistor, change this to false.
const bool FSR_NO_EXTERNAL_RESISTOR = true;

// ===================== FSR SETTINGS =====================

const int FSR_TOUCH_THRESHOLD = 250;
const int FSR_RELEASE_THRESHOLD = 120;

// ===================== TAP SETTINGS =====================

bool lastPiezoState = false;
unsigned long lastTapTime = 0;
int tapCount = 0;

const unsigned long TAP_DEBOUNCE_MS = 150;
const unsigned long TAP_SEQUENCE_GAP_MS = 500;

// ===================== HAPPINESS SETTINGS =====================

int happiness = 75;

const int HAPPINESS_MAX = 100;
const int HAPPINESS_MIN = 0;

// Complete soothing or meditation restores +30, not full recovery.
const int HAPPINESS_RECOVERY_AMOUNT = 30;

// Wake-up gives a small bonus only.
const int WAKE_HAPPINESS_BONUS = 3;

// Happiness decreases even during sleep.
const unsigned long HAPPINESS_DECAY_INTERVAL = 60000;
const int HAPPINESS_DECAY_AMOUNT = 2;

// ===================== COMFORT LEVEL SETTINGS =====================

const int COMFORT_LEVEL_1_THRESHOLD = 60;
const int COMFORT_LEVEL_2_THRESHOLD = 45;
const int COMFORT_LEVEL_3_THRESHOLD = 25;

const unsigned long SEEK_INTERVAL_LEVEL_1 = 60000;
const unsigned long SEEK_INTERVAL_LEVEL_2 = 30000;
const unsigned long SEEK_INTERVAL_LEVEL_3 = 15000;

const unsigned long SEEK_DURATION_LEVEL_1 = 2500;
const unsigned long SEEK_DURATION_LEVEL_2 = 4000;
const unsigned long SEEK_DURATION_LEVEL_3 = 6000;

const int SEEK_POWER_LEVEL_1 = 90;
const int SEEK_POWER_LEVEL_2 = 150;
const int SEEK_POWER_LEVEL_3 = 220;

unsigned long lastHappinessDecayTime = 0;
unsigned long lastSeekVibrationTime = 0;

bool seekingVibrationActive = false;
unsigned long seekingVibrationStartTime = 0;
unsigned long seekingVibrationDuration = 0;
int seekingVibrationPower = 0;

// ===================== SOOTHING SETTINGS =====================

int sootheCount = 0;
int sootheTarget = 6;

const int SOOTHE_TARGET_MIN = 5;
const int SOOTHE_TARGET_MAX = 7;

bool backWasTouched = false;
unsigned long lastSootheTime = 0;
const unsigned long SOOTHE_DEBOUNCE_MS = 350;

const unsigned long SOOTHE_TIMEOUT_MS = 15000;

const int SOOTHE_START_POWER = 230;
const int SOOTHE_END_POWER = 35;

// ===================== MODES =====================

enum DeviceMode {
  MODE_SLEEP,
  MODE_AWAKE,
  MODE_SEEKING_COMFORT,
  MODE_BEING_SOOTHED,
  MODE_CALM,
  MODE_MEDITATION
};

DeviceMode currentMode = MODE_SLEEP;

unsigned long modeStartTime = 0;
unsigned long lastInteractionTime = 0;
unsigned long lastPrintTime = 0;

// ===================== SENSOR VALUES =====================

int rawFSR = 0;
float smoothFSR = 0;

// ===================== MEDITATION SETTINGS =====================

const unsigned long MEDITATION_TOTAL_MS = 90000;

const unsigned long INHALE_MS = 7000;
const unsigned long PAUSE_AFTER_INHALE_MS = 500;
const unsigned long EXHALE_MS = 7000;
const unsigned long PAUSE_AFTER_EXHALE_MS = 500;

const unsigned long MEDITATION_CYCLE_MS =
  INHALE_MS + PAUSE_AFTER_INHALE_MS + EXHALE_MS + PAUSE_AFTER_EXHALE_MS;

const int MEDITATION_MIN_POWER = 65;
const int MEDITATION_MAX_POWER = 230;

int lastMeditationCycle = -1;

// ===================== SOUND WRAPPERS =====================
// These names keep the behaviour code readable.
// Audio implementation is separated in AudioPlayer.cpp.

void soundWake() {
  audio.play("/wake.wav");
}

void soundSleep() {
  audio.play("/sleep.wav");
}

void soundComfortLevel1() {
  audio.play("/comfort_1.wav");
}

void soundComfortLevel2() {
  audio.play("/comfort_2.wav");
}

void soundComfortLevel3() {
  audio.play("/comfort_3.wav");
}

void soundSootheStart() {
  audio.play("/soothe_start.wav");
}

void soundCalmDone() {
  audio.play("/calm_done.wav");
}

void soundMeditationStart() {
  audio.play("/meditation_start.wav");
}

void soundMeditationEnd() {
  audio.play("/meditation_end.wav");
}

// ===================== MOTOR FUNCTIONS =====================

void motorWrite(int power) {
  power = constrain(power, 0, 255);

  if (MOTOR_ACTIVE_HIGH) {
    analogWrite(MOTOR_PIN, power);
  } else {
    analogWrite(MOTOR_PIN, 255 - power);
  }
}

void motorOn() {
  motorWrite(255);
}

void motorOff() {
  if (MOTOR_ACTIVE_HIGH) {
    analogWrite(MOTOR_PIN, 0);
  } else {
    analogWrite(MOTOR_PIN, 255);
  }
}

void motorPulse(int durationMs) {
  motorOn();
  delay(durationMs);
  motorOff();
}

void wakeFeedback() {
  motorPulse(120);
  delay(100);
  motorPulse(80);
}

void sleepFeedback() {
  motorPulse(80);
}

void meditationStartFeedback() {
  motorPulse(150);
  delay(180);
  motorPulse(150);
  motorOff();
}

void meditationEndFeedback() {
  motorPulse(120);
  delay(180);
  motorPulse(120);
  motorOff();
}

void addHappiness(int amount) {
  happiness += amount;
  happiness = constrain(happiness, HAPPINESS_MIN, HAPPINESS_MAX);

  lastHappinessDecayTime = millis();

  Serial.print("Happiness recovered by ");
  Serial.print(amount);
  Serial.print(". Current happiness: ");
  Serial.println(happiness);
}

// ===================== COMFORT LEVEL HELPERS =====================

int getComfortLevel() {
  if (happiness < COMFORT_LEVEL_3_THRESHOLD) {
    return 3;
  } else if (happiness < COMFORT_LEVEL_2_THRESHOLD) {
    return 2;
  } else if (happiness < COMFORT_LEVEL_1_THRESHOLD) {
    return 1;
  } else {
    return 0;
  }
}

unsigned long getSeekIntervalByLevel(int level) {
  if (level == 3) return SEEK_INTERVAL_LEVEL_3;
  if (level == 2) return SEEK_INTERVAL_LEVEL_2;
  if (level == 1) return SEEK_INTERVAL_LEVEL_1;
  return 99999999;
}

unsigned long getSeekDurationByLevel(int level) {
  if (level == 3) return SEEK_DURATION_LEVEL_3;
  if (level == 2) return SEEK_DURATION_LEVEL_2;
  if (level == 1) return SEEK_DURATION_LEVEL_1;
  return 0;
}

int getSeekPowerByLevel(int level) {
  if (level == 3) return SEEK_POWER_LEVEL_3;
  if (level == 2) return SEEK_POWER_LEVEL_2;
  if (level == 1) return SEEK_POWER_LEVEL_1;
  return 0;
}

void playSeekingSoundByLevel(int level) {
  if (level == 3) {
    soundComfortLevel3();
  } else if (level == 2) {
    soundComfortLevel2();
  } else if (level == 1) {
    soundComfortLevel1();
  }
}

void stopSeekingVibration() {
  seekingVibrationActive = false;
  seekingVibrationStartTime = 0;
  seekingVibrationDuration = 0;
  seekingVibrationPower = 0;
  motorOff();
}

void startSeekingVibration(int level) {
  seekingVibrationActive = true;
  seekingVibrationStartTime = millis();
  seekingVibrationDuration = getSeekDurationByLevel(level);
  seekingVibrationPower = getSeekPowerByLevel(level);
  lastSeekVibrationTime = millis();

  Serial.print("Start seeking comfort vibration. Level: ");
  Serial.print(level);
  Serial.print(" | Duration: ");
  Serial.print(seekingVibrationDuration);
  Serial.print(" ms | Power: ");
  Serial.println(seekingVibrationPower);

  motorWrite(seekingVibrationPower);
  playSeekingSoundByLevel(level);
}

void updateSeekingVibration() {
  if (!seekingVibrationActive) {
    return;
  }

  unsigned long now = millis();

  if (now - seekingVibrationStartTime < seekingVibrationDuration) {
    motorWrite(seekingVibrationPower);
  } else {
    stopSeekingVibration();
  }
}

// ===================== SOOTHING VIBRATION HELPERS =====================

int getContinuousSoothePower() {
  if (sootheTarget <= 1) {
    return 0;
  }

  if (sootheCount >= sootheTarget) {
    return 0;
  }

  float progress = (float)(sootheCount - 1) / (float)(sootheTarget - 1);
  progress = constrain(progress, 0.0, 1.0);

  int power = SOOTHE_START_POWER - (int)(progress * (SOOTHE_START_POWER - SOOTHE_END_POWER));
  power = constrain(power, SOOTHE_END_POWER, SOOTHE_START_POWER);

  return power;
}

void updateContinuousSoothingVibration() {
  int power = getContinuousSoothePower();
  motorWrite(power);
}

// ===================== MODE FUNCTION =====================

void setMode(DeviceMode newMode) {
  currentMode = newMode;
  modeStartTime = millis();

  if (newMode != MODE_SEEKING_COMFORT) {
    seekingVibrationActive = false;
  }

  if (
    newMode != MODE_MEDITATION &&
    newMode != MODE_BEING_SOOTHED &&
    newMode != MODE_SEEKING_COMFORT
  ) {
    motorOff();
  }

  if (newMode == MODE_SEEKING_COMFORT) {
    lastSeekVibrationTime = 0;
  }

  Serial.print("Mode changed to: ");

  if (newMode == MODE_SLEEP) {
    Serial.println("SLEEP");
  } else if (newMode == MODE_AWAKE) {
    Serial.println("AWAKE");
  } else if (newMode == MODE_SEEKING_COMFORT) {
    Serial.println("SEEKING_COMFORT");
  } else if (newMode == MODE_BEING_SOOTHED) {
    Serial.println("BEING_SOOTHED");
  } else if (newMode == MODE_CALM) {
    Serial.println("CALM");
  } else if (newMode == MODE_MEDITATION) {
    Serial.println("MEDITATION");
    lastMeditationCycle = -1;
  }
}

// ===================== SENSOR READING =====================

int readFSR() {
  if (FSR_NO_EXTERNAL_RESISTOR) {
    pinMode(FSR_PIN, OUTPUT);
    digitalWrite(FSR_PIN, LOW);
    delay(3);

    pinMode(FSR_PIN, INPUT_PULLDOWN);
    delay(2);

    return analogRead(FSR_PIN);
  } else {
    return analogRead(FSR_PIN);
  }
}

void readPiezoTap() {
  bool currentState = digitalRead(PIEZO_PIN);

  if (!PIEZO_ACTIVE_HIGH) {
    currentState = !currentState;
  }

  unsigned long now = millis();

  if (currentState && !lastPiezoState) {
    if (now - lastTapTime > TAP_DEBOUNCE_MS) {
      tapCount++;
      lastTapTime = now;

      Serial.print("Head tap detected. Count: ");
      Serial.println(tapCount);
    }
  }

  lastPiezoState = currentState;
}

// ===================== HEAD TAP LOGIC =====================

void handleHeadTapLogic() {
  readPiezoTap();

  if (tapCount == 0) {
    return;
  }

  unsigned long now = millis();

  if (now - lastTapTime > TAP_SEQUENCE_GAP_MS) {
    int finalTapCount = tapCount;
    tapCount = 0;

    if (finalTapCount == 1) {
      lastInteractionTime = now;

      if (currentMode == MODE_SLEEP) {
        Serial.println("Single head tap: wake up");

        setMode(MODE_AWAKE);
        wakeFeedback();
        soundWake();
        addHappiness(WAKE_HAPPINESS_BONUS);

      } else if (currentMode == MODE_AWAKE || currentMode == MODE_CALM) {
        Serial.println("Single head tap: go to sleep");

        setMode(MODE_SLEEP);
        sleepFeedback();
        soundSleep();

      } else if (currentMode == MODE_SEEKING_COMFORT) {
        Serial.println("Single head tap: temporarily acknowledge and sleep");

        stopSeekingVibration();
        setMode(MODE_SLEEP);
        sleepFeedback();
        soundSleep();

      } else if (currentMode == MODE_BEING_SOOTHED) {
        Serial.println("Single head tap ignored during soothing.");

      } else if (currentMode == MODE_MEDITATION) {
        Serial.println("Single head tap ignored during meditation.");
      }

    } else if (finalTapCount == 2) {
      Serial.println("Double head tap: start 90s meditation");

      lastInteractionTime = now;

      stopSeekingVibration();
      setMode(MODE_MEDITATION);

      meditationStartFeedback();
      soundMeditationStart();

      modeStartTime = millis();
      lastMeditationCycle = -1;

    } else {
      Serial.println("More than two taps: wake confirmation");

      lastInteractionTime = now;
      setMode(MODE_AWAKE);
      wakeFeedback();
      soundWake();
    }
  }
}

// ===================== HAPPINESS LOGIC =====================

void updateHappiness() {
  // Happiness still decreases in sleep.
  // It does not decrease during meditation, active soothing, or calm.
  if (
    currentMode == MODE_MEDITATION ||
    currentMode == MODE_BEING_SOOTHED ||
    currentMode == MODE_CALM
  ) {
    return;
  }

  unsigned long now = millis();

  if (now - lastHappinessDecayTime > HAPPINESS_DECAY_INTERVAL) {
    lastHappinessDecayTime = now;

    happiness -= HAPPINESS_DECAY_AMOUNT;
    happiness = constrain(happiness, HAPPINESS_MIN, HAPPINESS_MAX);

    Serial.print("Happiness decreased to: ");
    Serial.println(happiness);

    int comfortLevel = getComfortLevel();

    if (comfortLevel > 0 && currentMode != MODE_SEEKING_COMFORT) {
      Serial.print("Happiness low. Plush wakes up and seeks comfort. Level: ");
      Serial.println(comfortLevel);

      setMode(MODE_SEEKING_COMFORT);
    }
  }
}

// ===================== BACK FSR SOOTHING LOGIC =====================

void resetSoothingSession() {
  sootheCount = 0;
  sootheTarget = random(SOOTHE_TARGET_MIN, SOOTHE_TARGET_MAX + 1);

  Serial.print("New soothing target: ");
  Serial.println(sootheTarget);
}

void handleBackSoothing() {
  unsigned long now = millis();

  bool backTouched = smoothFSR >= FSR_TOUCH_THRESHOLD;
  bool backReleased = smoothFSR <= FSR_RELEASE_THRESHOLD;

  if (backTouched && !backWasTouched) {
    if (now - lastSootheTime > SOOTHE_DEBOUNCE_MS) {
      lastSootheTime = now;
      lastInteractionTime = now;

      if (
        currentMode == MODE_SEEKING_COMFORT ||
        currentMode == MODE_AWAKE ||
        currentMode == MODE_BEING_SOOTHED
      ) {
        if (currentMode != MODE_BEING_SOOTHED) {
          resetSoothingSession();

          stopSeekingVibration();

          setMode(MODE_BEING_SOOTHED);
          soundSootheStart();
        }

        sootheCount++;

        Serial.print("Back soothing stroke: ");
        Serial.print(sootheCount);
        Serial.print("/");
        Serial.println(sootheTarget);

        if (sootheCount >= sootheTarget) {
          Serial.println("Soothing completed. Plush is calmer.");

          motorOff();

          addHappiness(HAPPINESS_RECOVERY_AMOUNT);

          soundCalmDone();
          setMode(MODE_CALM);
        }
      }
    }
  }

  if (backReleased) {
    backWasTouched = false;
  } else if (backTouched) {
    backWasTouched = true;
  }
}

// ===================== MODE UPDATES =====================

void updateSleepMode() {
  motorOff();
}

void updateAwakeMode() {
  motorOff();
}

void updateSeekingComfortMode() {
  unsigned long now = millis();

  int comfortLevel = getComfortLevel();

  if (comfortLevel == 0) {
    Serial.println("Happiness is enough. Stop seeking comfort.");
    stopSeekingVibration();
    setMode(MODE_AWAKE);
    return;
  }

  unsigned long currentSeekInterval = getSeekIntervalByLevel(comfortLevel);

  if (seekingVibrationActive) {
    updateSeekingVibration();
    return;
  }

  if (lastSeekVibrationTime == 0 || now - lastSeekVibrationTime > currentSeekInterval) {
    startSeekingVibration(comfortLevel);
  } else {
    motorOff();
  }
}

void updateBeingSoothedMode() {
  unsigned long now = millis();

  if (sootheCount >= sootheTarget) {
    motorOff();
    return;
  }

  if (now - lastSootheTime > SOOTHE_TIMEOUT_MS && sootheCount < sootheTarget) {
    Serial.println("Soothing stopped before completion.");
    motorOff();
    setMode(MODE_SEEKING_COMFORT);
    return;
  }

  updateContinuousSoothingVibration();
}

void updateCalmMode() {
  motorOff();

  unsigned long now = millis();

  if (now - modeStartTime > 15000) {
    setMode(MODE_AWAKE);
  }
}

// ===================== MEDITATION LOGIC =====================

void updateMeditationMode() {
  unsigned long now = millis();
  unsigned long elapsed = now - modeStartTime;

  if (elapsed >= MEDITATION_TOTAL_MS) {
    Serial.println("Meditation completed.");

    motorOff();

    meditationEndFeedback();
    soundMeditationEnd();

    addHappiness(HAPPINESS_RECOVERY_AMOUNT);

    setMode(MODE_CALM);

    return;
  }

  int currentCycle = elapsed / MEDITATION_CYCLE_MS;
  unsigned long cycleTime = elapsed % MEDITATION_CYCLE_MS;

  if (currentCycle != lastMeditationCycle) {
    lastMeditationCycle = currentCycle;

    Serial.print("Meditation cycle ");
    Serial.print(currentCycle + 1);
    Serial.println(" started.");
  }

  unsigned long inhaleStart = 0;
  unsigned long inhaleEnd = inhaleStart + INHALE_MS;

  unsigned long pauseAfterInhaleStart = inhaleEnd;
  unsigned long pauseAfterInhaleEnd = pauseAfterInhaleStart + PAUSE_AFTER_INHALE_MS;

  unsigned long exhaleStart = pauseAfterInhaleEnd;
  unsigned long exhaleEnd = exhaleStart + EXHALE_MS;

  unsigned long pauseAfterExhaleStart = exhaleEnd;
  unsigned long pauseAfterExhaleEnd = pauseAfterExhaleStart + PAUSE_AFTER_EXHALE_MS;

  if (cycleTime >= inhaleStart && cycleTime < inhaleEnd) {
    float progress = (float)(cycleTime - inhaleStart) / (float)INHALE_MS;
    progress = constrain(progress, 0.0, 1.0);

    int power = MEDITATION_MIN_POWER + (int)(progress * (MEDITATION_MAX_POWER - MEDITATION_MIN_POWER));
    motorWrite(power);

  } else if (cycleTime >= pauseAfterInhaleStart && cycleTime < pauseAfterInhaleEnd) {
    motorOff();

  } else if (cycleTime >= exhaleStart && cycleTime < exhaleEnd) {
    float progress = (float)(cycleTime - exhaleStart) / (float)EXHALE_MS;
    progress = constrain(progress, 0.0, 1.0);

    int power = MEDITATION_MAX_POWER - (int)(progress * (MEDITATION_MAX_POWER - MEDITATION_MIN_POWER));
    motorWrite(power);

  } else if (cycleTime >= pauseAfterExhaleStart && cycleTime < pauseAfterExhaleEnd) {
    motorOff();

  } else {
    motorOff();
  }
}

// ===================== DEBUG =====================

String getModeName() {
  if (currentMode == MODE_SLEEP) return "SLEEP";
  if (currentMode == MODE_AWAKE) return "AWAKE";
  if (currentMode == MODE_SEEKING_COMFORT) return "SEEKING_COMFORT";
  if (currentMode == MODE_BEING_SOOTHED) return "BEING_SOOTHED";
  if (currentMode == MODE_CALM) return "CALM";
  if (currentMode == MODE_MEDITATION) return "MEDITATION";
  return "UNKNOWN";
}

void printDebug() {
  unsigned long now = millis();

  if (now - lastPrintTime > 1000) {
    lastPrintTime = now;

    Serial.print("Raw FSR: ");
    Serial.print(rawFSR);

    Serial.print(" | Smooth FSR: ");
    Serial.print((int)smoothFSR);

    Serial.print(" | Happiness: ");
    Serial.print(happiness);

    Serial.print(" | Comfort Level: ");
    Serial.print(getComfortLevel());

    Serial.print(" | Soothe: ");
    Serial.print(sootheCount);
    Serial.print("/");
    Serial.print(sootheTarget);

    Serial.print(" | Mode: ");
    Serial.println(getModeName());
  }
}

// ===================== SETUP / LOOP =====================

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println();
  Serial.println("=====================================");
  Serial.println("Emotional Plush Prototype - WAV Audio Version");
  Serial.println("1 head tap = sleep / wake toggle");
  Serial.println("2 head taps = 90s meditation");
  Serial.println("Back FSR = continuous fading soothing");
  Serial.println("Sound = WAV files in LittleFS");
  Serial.println("=====================================");

  pinMode(MOTOR_PIN, OUTPUT);
  motorOff();

  pinMode(PIEZO_PIN, INPUT);

  analogReadResolution(12);

  randomSeed(analogRead(FSR_PIN));

  audio.begin();

  lastInteractionTime = millis();
  lastHappinessDecayTime = millis();

  setMode(MODE_SLEEP);

  Serial.println("Motor startup test...");
  motorPulse(500);
  delay(300);
  motorPulse(300);
  motorOff();

  Serial.println("Sound startup test...");
  soundWake();

  Serial.println("If motor vibrated and wake.wav played, hardware and LittleFS audio are OK.");
  Serial.println("-------------------------------------");
}

void loop() {
  rawFSR = readFSR();
  smoothFSR = smoothFSR * 0.65 + rawFSR * 0.35;

  handleHeadTapLogic();
  handleBackSoothing();

  updateHappiness();

  if (currentMode == MODE_SLEEP) {
    updateSleepMode();
  } else if (currentMode == MODE_AWAKE) {
    updateAwakeMode();
  } else if (currentMode == MODE_SEEKING_COMFORT) {
    updateSeekingComfortMode();
  } else if (currentMode == MODE_BEING_SOOTHED) {
    updateBeingSoothedMode();
  } else if (currentMode == MODE_CALM) {
    updateCalmMode();
  } else if (currentMode == MODE_MEDITATION) {
    updateMeditationMode();
  }

  printDebug();

  delay(30);
}
