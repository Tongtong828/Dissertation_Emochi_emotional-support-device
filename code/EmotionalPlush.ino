#include <Arduino.h>
#include "AudioPlayer.h"

// PINS 

const int FSR_PIN = 1;
const int PIEZO_PIN = 4;
const int MOTOR_PIN = 5;

const int I2S_DOUT = 18;
const int I2S_BCLK = 17;
const int I2S_LRC  = 16;

AudioPlayer audio(I2S_DOUT, I2S_BCLK, I2S_LRC);

// BASIC SETTINGS 

const bool MOTOR_ACTIVE_HIGH = true;
const bool PIEZO_ACTIVE_HIGH = true;

const bool FSR_NO_EXTERNAL_RESISTOR = true;

// FSR SETTINGS 

const int FSR_TOUCH_THRESHOLD = 150;
const int FSR_RELEASE_THRESHOLD = 70;

// TAP SETTINGS 

bool lastPiezoState = false;
unsigned long lastTapTime = 0;
int tapCount = 0;

const unsigned long TAP_DEBOUNCE_MS = 40;      // easier to catch two continuous taps
const unsigned long TAP_SEQUENCE_GAP_MS = 1200; // longer window for double tap

// HAPPINESS SETTINGS 

int happiness = 65;

const int HAPPINESS_MAX = 100;
const int HAPPINESS_MIN = 0;

const int HAPPINESS_RECOVERY_AMOUNT = 30;
const int WAKE_HAPPINESS_BONUS = 3;······1`

// happy will keep decreasing over time, including during sleep
const unsigned long HAPPINESS_DECAY_INTERVAL = 60000;
const int HAPPINESS_DECAY_AMOUNT = 2;

// AUTO SLEEP / SNOOZE SETTINGS 

// In the AWAKE state, there is no interaction for 90 seconds and it automatically goes into sleep mode
const unsigned long AUTO_SLEEP_AFTER_MS = 90000;

// seeking comfort, simply patting the top of your head can pause the initiative to disturb for five minutes
const unsigned long SNOOZE_DURATION_MS = 300000;
unsigned long snoozeUntil = 0;

// COMFORT LEVEL SETTINGS

const int COMFORT_LEVEL_1_THRESHOLD = 60;
const int COMFORT_LEVEL_2_THRESHOLD = 45;
const int COMFORT_LEVEL_3_THRESHOLD = 25;

const unsigned long SEEK_INTERVAL_LEVEL_1 = 60000;
const unsigned long SEEK_INTERVAL_LEVEL_2 = 30000;
const unsigned long SEEK_INTERVAL_LEVEL_3 = 15000;

const unsigned long SEEK_DURATION_LEVEL_1 = 2500;
const unsigned long SEEK_DURATION_LEVEL_2 = 4000;
const unsigned long SEEK_DURATION_LEVEL_3 = 6000;

const int SEEK_POWER_LEVEL_1 = 255;
const int SEEK_POWER_LEVEL_2 = 255;
const int SEEK_POWER_LEVEL_3 = 255;

unsigned long lastHappinessDecayTime = 0;
unsigned long lastSeekVibrationTime = 0;

bool seekingVibrationActive = false;
unsigned long seekingVibrationStartTime = 0;
unsigned long seekingVibrationDuration = 0;
int seekingVibrationPower = 0;

// SOOTHING SETTINGS 

int sootheCount = 0;
int sootheTarget = 6;

const int SOOTHE_TARGET_MIN = 5;
const int SOOTHE_TARGET_MAX = 7;

bool backWasTouched = false;
unsigned long lastSootheTime = 0;
const unsigned long SOOTHE_DEBOUNCE_MS = 350;

const unsigned long SOOTHE_TIMEOUT_MS = 15000;

const int SOOTHE_START_POWER = 255;
const int SOOTHE_END_POWER = 120;

// MODES 

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

// SENSOR VALUES 

int rawFSR = 0;
float smoothFSR = 0;

//  MEDITATION SETTINGS

const unsigned long MEDITATION_TOTAL_MS = 90000;

const unsigned long INHALE_MS = 5000;
const unsigned long PAUSE_AFTER_INHALE_MS = 500;
const unsigned long EXHALE_MS = 5000;
const unsigned long PAUSE_AFTER_EXHALE_MS = 500;

const unsigned long MEDITATION_CYCLE_MS =
  INHALE_MS + PAUSE_AFTER_INHALE_MS + EXHALE_MS + PAUSE_AFTER_EXHALE_MS;

const int MEDITATION_MIN_POWER = 120;
const int MEDITATION_MAX_POWER = 255;
int lastMeditationCycle = -1;

// SOUND WRAPPERS 

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

// MOTOR FUNCTIONS 

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
  motorWrite(255);
  delay(220);
  motorOff();
  delay(120);
  motorWrite(255);
  delay(180);
  motorOff();
}

void sleepFeedback() {
  motorWrite(255);
  delay(180);
  motorOff();
}

void sootheStrokeFeedback() {
  motorWrite(255);
  delay(120);
  motorOff();
}

void meditationStartFeedback() {
  motorWrite(255);
  delay(260);
  motorOff();
  delay(180);
  motorWrite(255);
  delay(260);
  motorOff();
}

void meditationEndFeedback() {
  motorWrite(255);
  delay(220);
  motorOff();
  delay(180);
  motorWrite(255);
  delay(220);
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

// COMFORT LEVEL HELPERS 

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

// SOOTHING VIBRATION HELPERS 

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

//  MODE FUNCTION 

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

//  SENSOR READING

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

//  HEAD TAP LOGIC 

void handleHeadTapLogic() {
  readPiezoTap();

  if (currentMode == MODE_MEDITATION) {
    if (tapCount > 0) {
      tapCount = 0;
      Serial.println("Head tap ignored during meditation.");
    }
    return;
  }

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
        Serial.println("Single head tap: already awake, no wake behavior.");

      } else if (currentMode == MODE_SEEKING_COMFORT) {
        Serial.println("Single head tap: snooze comfort request");

        // not responded for the time being
        stopSeekingVibration();
        snoozeUntil = now + SNOOZE_DURATION_MS;

        setMode(MODE_AWAKE);

        sleepFeedback();
        soundSleep();

        Serial.println("Comfort request snoozed for 5 minutes.");

      } else if (currentMode == MODE_BEING_SOOTHED) {
        Serial.println("Single head tap ignored during soothing.");
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

//  HAPPINESS LOGIC

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
      if (now < snoozeUntil) {
        Serial.println("Comfort request is snoozed. Not seeking comfort now.");
        return;
      }

      Serial.print("Happiness low. Plush wakes up and seeks comfort. Level: ");
      Serial.println(comfortLevel);

      setMode(MODE_SEEKING_COMFORT);
    }
  }
}

//  BACK FSR SOOTHING LOGIC 

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

  // Detect the rising edge before updating backWasTouched.
  // This prevents random FSR noise in AWAKE mode from starting a soothing session.
  bool newBackTouch = backTouched && !backWasTouched;

  if (backReleased) {
    backWasTouched = false;
  } else if (backTouched) {
    backWasTouched = true;
  }

  // Important fix:
  // FSR can only start/count soothing when the plush is already seeking comfort
  // or already being soothed.
  // When happiness is high and mode is AWAKE, touching the back will NOT trigger soothing.
  if (
    currentMode != MODE_SEEKING_COMFORT &&
    currentMode != MODE_BEING_SOOTHED
  ) {
    return;
  }

  if (newBackTouch) {
    if (now - lastSootheTime > SOOTHE_DEBOUNCE_MS) {
      lastSootheTime = now;
      lastInteractionTime = now;

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

      sootheStrokeFeedback();

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

//  MODE UPDATES 

void updateSleepMode() {
  motorOff();
}

void updateAwakeMode() {
  motorOff();

  unsigned long now = millis();

  if (now - lastInteractionTime > AUTO_SLEEP_AFTER_MS) {
    Serial.println("No interaction: auto sleep.");

    setMode(MODE_SLEEP);
    sleepFeedback();
    soundSleep();
  }
}

void updateSeekingComfortMode() {
  unsigned long now = millis();

  int comfortLevel = getComfortLevel();

  if (comfortLevel == 0) {
    Serial.println("Happiness is enough. Stop seeking comfort.");
    stopSeekingVibration();
    lastInteractionTime = now;
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

  motorOff();
}

void updateCalmMode() {
  motorOff();

  unsigned long now = millis();

  if (now - modeStartTime > 15000) {
    lastInteractionTime = now;
    setMode(MODE_AWAKE);
  }
}

//  MEDITATION LOGIC 

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

    Serial.print("Meditation breathing cycle ");
    Serial.print(currentCycle + 1);
    Serial.println(" started: inhale weak->strong, exhale strong->weak.");
  }

  unsigned long inhaleEnd = INHALE_MS;
  unsigned long pauseAfterInhaleEnd = inhaleEnd + PAUSE_AFTER_INHALE_MS;
  unsigned long exhaleEnd = pauseAfterInhaleEnd + EXHALE_MS;

  if (cycleTime < inhaleEnd) {
    float progress = (float)cycleTime / (float)INHALE_MS;
    progress = constrain(progress, 0.0, 1.0);

    int power = MEDITATION_MIN_POWER +
      (int)(progress * (MEDITATION_MAX_POWER - MEDITATION_MIN_POWER));

    motorWrite(power);

  } else if (cycleTime < pauseAfterInhaleEnd) {
    motorOff();

  } else if (cycleTime < exhaleEnd) {
    unsigned long exhaleTime = cycleTime - pauseAfterInhaleEnd;
    float progress = (float)exhaleTime / (float)EXHALE_MS;
    progress = constrain(progress, 0.0, 1.0);

    int power = MEDITATION_MAX_POWER -
      (int)(progress * (MEDITATION_MAX_POWER - MEDITATION_MIN_POWER));

    motorWrite(power);

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

//  SETUP / LOOP 

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println();
  Serial.println("=====================================");
  Serial.println("Emotional Plush Prototype - WAV Audio Version");
  Serial.println("1 head tap = wake / gentle response");
  Serial.println("Auto sleep after no interaction");
  Serial.println("1 head tap during seeking comfort = snooze");
  Serial.println("2 head taps = 90s meditation");
  Serial.println("Back FSR = soothing only after seeking comfort");
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