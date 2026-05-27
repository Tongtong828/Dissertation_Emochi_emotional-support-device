/*
  Emotional Plush Prototype
  Updated Logic:
  - Power on = device starts
  - 1 head tap = toggle sleep / awake
  - 2 head taps = 90-second meditation
  - Happiness decreases over time even during sleep
  - Comfort seeking has 3 levels:
      Level 1: happiness < 60, gentle seeking every 60s, vibrates for 2.5s
      Level 2: happiness < 45, medium seeking every 30s, vibrates for 4s
      Level 3: happiness < 25, strong seeking every 15s, vibrates for 6s
  - Back FSR soothing:
      First touch starts continuous vibration
      As the user soothes 5-7 times, vibration gradually weakens until disappearing
  - Meditation:
      Start cue twice
      Inhale 7s weak-to-strong
      Pause 0.5s
      Exhale 7s strong-to-weak
      Pause 0.5s
      Repeat for about 90s
      End cue twice
*/

#include <Arduino.h>

// ===================== PINS =====================

const int FSR_PIN = 1;
const int PIEZO_PIN = 4;
const int MOTOR_PIN = 5;

// ===================== BASIC SETTINGS =====================

const bool MOTOR_ACTIVE_HIGH = true;
const bool PIEZO_ACTIVE_HIGH = true;
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

const int HAPPINESS_RECOVERY_AMOUNT = 30;
const int WAKE_HAPPINESS_BONUS = 3;

// happy 会一直随时间下降，包括睡眠状态
const unsigned long HAPPINESS_DECAY_INTERVAL = 60000;
const int HAPPINESS_DECAY_AMOUNT = 2;

// ===================== COMFORT LEVEL SETTINGS =====================

const int COMFORT_LEVEL_1_THRESHOLD = 60;
const int COMFORT_LEVEL_2_THRESHOLD = 45;
const int COMFORT_LEVEL_3_THRESHOLD = 25;

const unsigned long SEEK_INTERVAL_LEVEL_1 = 60000;
const unsigned long SEEK_INTERVAL_LEVEL_2 = 30000;
const unsigned long SEEK_INTERVAL_LEVEL_3 = 15000;

// 每次寻求安慰的持续震动时间
const unsigned long SEEK_DURATION_LEVEL_1 = 2500;
const unsigned long SEEK_DURATION_LEVEL_2 = 4000;
const unsigned long SEEK_DURATION_LEVEL_3 = 6000;

// 每个 level 寻求安慰时的震动强度
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

// 用户停止安抚超过这个时间，回到寻求安慰
const unsigned long SOOTHE_TIMEOUT_MS = 15000;

// 安抚开始时的持续震动强度，以及最低强度
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

void calmFeedback() {
  motorPulse(70);
  delay(120);
  motorPulse(70);
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

  // 恢复后重置下降计时，避免刚安抚完马上又下降
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

  // 第一次触摸时 progress = 0，最强
  // 接近完成时 progress 接近 1，最弱
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

  // 进入非寻求安慰模式时，停止寻求安慰震动
  if (newMode != MODE_SEEKING_COMFORT) {
    seekingVibrationActive = false;
  }

  // 冥想和安抚模式由自己的 update 函数控制马达
  if (
    newMode != MODE_MEDITATION &&
    newMode != MODE_BEING_SOOTHED &&
    newMode != MODE_SEEKING_COMFORT
  ) {
    motorOff();
  }

  if (newMode == MODE_SEEKING_COMFORT) {
    // 进入寻求安慰模式后，允许立即震动一次
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
        addHappiness(WAKE_HAPPINESS_BONUS);

        Serial.println("[Future voice] I am awake.");

      } else if (currentMode == MODE_AWAKE || currentMode == MODE_CALM) {
        Serial.println("Single head tap: go to sleep");

        setMode(MODE_SLEEP);
        sleepFeedback();

        Serial.println("[Future voice] I am going to sleep.");

      } else if (currentMode == MODE_SEEKING_COMFORT) {
        Serial.println("Single head tap: temporarily acknowledge and sleep");

        setMode(MODE_SLEEP);
        sleepFeedback();

        Serial.println("[Future voice] I will rest for now.");

      } else if (currentMode == MODE_BEING_SOOTHED) {
        Serial.println("Single head tap ignored during soothing.");

      } else if (currentMode == MODE_MEDITATION) {
        Serial.println("Single head tap ignored during meditation.");
      }

    } else if (finalTapCount == 2) {
      Serial.println("Double head tap: start 90s meditation");

      lastInteractionTime = now;

      setMode(MODE_MEDITATION);

      Serial.println("[Future voice] Meditation will start soon.");

      meditationStartFeedback();

      // 开始提示之后，重新计时，保证正式冥想约 90 秒
      modeStartTime = millis();
      lastMeditationCycle = -1;

    } else {
      Serial.println("More than two taps: wake confirmation");

      lastInteractionTime = now;
      setMode(MODE_AWAKE);
      wakeFeedback();
    }
  }
}

// ===================== HAPPINESS LOGIC =====================

void updateHappiness() {
  // 冥想、被安抚、平静状态下不下降
  // 睡眠状态下依然下降
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

      Serial.println("[Future voice] Can you soothe my back?");

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

  // 一次安抚动作 = FSR 从松开到被按下
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

          // 用户第一次触摸 FSR 后，停止寻求安慰震动，转入持续安抚震动
          stopSeekingVibration();

          setMode(MODE_BEING_SOOTHED);
        }

        sootheCount++;

        Serial.print("Back soothing stroke: ");
        Serial.print(sootheCount);
        Serial.print("/");
        Serial.println(sootheTarget);

        if (sootheCount >= sootheTarget) {
          Serial.println("Soothing completed. Plush is calmer.");
          Serial.println("[Future voice] I feel calmer now.");

          motorOff();

          addHappiness(HAPPINESS_RECOVERY_AMOUNT);

          setMode(MODE_CALM);

          // 这里不再额外震动，保持“逐渐消失”的效果
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

  // 不再自动睡眠
  // 睡眠由压电片单拍控制
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

  // 如果当前正在震动，就持续更新震动，直到本次寻求安慰结束
  if (seekingVibrationActive) {
    updateSeekingVibration();
    return;
  }

  // 如果没有正在震动，检查是否到了下一次寻求安慰的时间
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

  // 安抚中断太久，回到寻求安慰
  if (now - lastSootheTime > SOOTHE_TIMEOUT_MS && sootheCount < sootheTarget) {
    Serial.println("Soothing stopped before completion.");
    motorOff();
    setMode(MODE_SEEKING_COMFORT);
    return;
  }

  // 核心变化：
  // 用户开始安抚后，马达持续震动；
  // 每完成一次安抚，sootheCount 增加，震动强度逐渐降低。
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
    Serial.println("[Future voice] Meditation completed.");

    motorOff();

    meditationEndFeedback();

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
    // 吸气：连续震动由弱到强
    float progress = (float)(cycleTime - inhaleStart) / (float)INHALE_MS;
    progress = constrain(progress, 0.0, 1.0);

    int power = MEDITATION_MIN_POWER + (int)(progress * (MEDITATION_MAX_POWER - MEDITATION_MIN_POWER));
    motorWrite(power);

  } else if (cycleTime >= pauseAfterInhaleStart && cycleTime < pauseAfterInhaleEnd) {
    // 吸气结束后短暂停一下
    motorOff();

  } else if (cycleTime >= exhaleStart && cycleTime < exhaleEnd) {
    // 呼气：连续震动由强到弱
    float progress = (float)(cycleTime - exhaleStart) / (float)EXHALE_MS;
    progress = constrain(progress, 0.0, 1.0);

    int power = MEDITATION_MAX_POWER - (int)(progress * (MEDITATION_MAX_POWER - MEDITATION_MIN_POWER));
    motorWrite(power);

  } else if (cycleTime >= pauseAfterExhaleStart && cycleTime < pauseAfterExhaleEnd) {
    // 呼气结束后短暂停一下，然后直接进入下一轮吸气
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
  Serial.println("Emotional Plush Prototype");
  Serial.println("Power on = device starts");
  Serial.println("1 head tap = sleep / wake toggle");
  Serial.println("2 head taps = 90s meditation");
  Serial.println("Seeking comfort now vibrates for a duration");
  Serial.println("Back soothing now uses continuous fading vibration");
  Serial.println("=====================================");

  pinMode(MOTOR_PIN, OUTPUT);
  motorOff();

  pinMode(PIEZO_PIN, INPUT);

  analogReadResolution(12);

  randomSeed(analogRead(FSR_PIN));

  lastInteractionTime = millis();
  lastHappinessDecayTime = millis();

  setMode(MODE_SLEEP);

  Serial.println("Motor startup test...");
  motorPulse(500);
  delay(300);
  motorPulse(300);
  motorOff();

  Serial.println("If motor vibrated, motor wiring is OK.");
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