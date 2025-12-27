// Full firmware for XIAO nRF52840 tuner with button control
#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <math.h>

// ========== OLED ==========
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire);

// ========== PINS ==========
const int AUDIO_ANALOG_PIN = A0;  // Jack input via 09B16
const int MIC_I2S_WS_PIN  = D10;
const int MIC_I2S_SD_PIN  = D9;
const int MIC_I2S_SCK_PIN = D8;

const int LED_FLAT_PIN   = D1;
const int LED_CENTER_PIN = D2;
const int LED_SHARP_PIN  = D3;

// ---- BUTTONS ----
const int BTN_UP_PIN   = D6;  // increase Hz
const int BTN_DOWN_PIN = D7;  // decrease Hz

// ========== CONFIG ==========
float targetFrequency = 440.0f;  // now variable, not constant
const float CENTER_TOLERANCE_CENTS = 5.0f;
const float ANALOG_SAMPLE_RATE = 8000.0f;
const float I2S_SAMPLE_RATE    = 16000.0f;
const int   BUFFER_SIZE        = 512;
const int JACK_ACTIVITY_THRESHOLD = 20;

enum class AudioSource { JackAnalog, I2SMic };

// ========== BUTTON HANDLING ==========
bool readButton(int pin) {
  return digitalRead(pin) == LOW;  // assuming buttons pull to GND
}

void handleButtons() {
  static unsigned long lastPress = 0;
  unsigned long now = millis();

  if (now - lastPress < 150) return;  // debounce

  if (readButton(BTN_UP_PIN)) {
    targetFrequency += 1.0f;
    lastPress = now;
  }

  if (readButton(BTN_DOWN_PIN)) {
    targetFrequency -= 1.0f;
    if (targetFrequency < 1) targetFrequency = 1;  // prevent negative/zero
    lastPress = now;
  }
}

// ========== SETUP ==========
void setup() {
  Serial.begin(115200);
  delay(500);
  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  display.clearDisplay();
  display.display();

  pinMode(LED_FLAT_PIN,   OUTPUT);
  pinMode(LED_CENTER_PIN, OUTPUT);
  pinMode(LED_SHARP_PIN,  OUTPUT);
  pinMode(AUDIO_ANALOG_PIN, INPUT);

  // Buttons
  pinMode(BTN_UP_PIN,   INPUT_PULLUP);
  pinMode(BTN_DOWN_PIN, INPUT_PULLUP);

  display.setTextSize(2);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println("Tuner");
  display.setTextSize(1);
  display.println("XIAO nRF52840");
  display.display();
  delay(1000);
}

// ========== MAIN LOOP ==========
void loop() {
  handleButtons();  // <-- NEW

  static int16_t buffer[BUFFER_SIZE];
  AudioSource source = selectAudioSource();

  bool ok = false;
  if (source == AudioSource::JackAnalog) {
    ok = captureAnalogBuffer(buffer, BUFFER_SIZE);
  } else {
    ok = captureI2SBuffer(buffer, BUFFER_SIZE);
  }

  if (!ok) {
    showError("Audio capture fail");
    delay(100);
    return;
  }

  float sampleRate = (source == AudioSource::JackAnalog) ? ANALOG_SAMPLE_RATE : I2S_SAMPLE_RATE;
  float freq = estimateFrequency(buffer, BUFFER_SIZE, sampleRate);
  float cents = computeCentsDifference(freq, targetFrequency);

  updateTuningLeds(cents);
  updateTunerDisplay(freq, cents, source);
  delay(20);
}

// ========== AUDIO ==========
AudioSource selectAudioSource() {
  long sumAbs = 0;
  for (int i = 0; i < 64; ++i) {
    int raw = analogRead(AUDIO_ANALOG_PIN);
    sumAbs += abs(raw - 2048);
    delayMicroseconds(100);
  }
  return (sumAbs / 64 > JACK_ACTIVITY_THRESHOLD) ? AudioSource::JackAnalog : AudioSource::I2SMic;
}

bool captureAnalogBuffer(int16_t* buffer, int length) {
  unsigned long period = 1000000.0f / ANALOG_SAMPLE_RATE;
  unsigned long next = micros();
  for (int i = 0; i < length; ++i) {
    while ((long)(micros() - next) < 0);
    buffer[i] = analogRead(AUDIO_ANALOG_PIN) - 2048;
    next += period;
  }
  return true;
}

bool captureI2SBuffer(int16_t* buffer, int length) {
  // TODO: Replace with actual I2S mic capture code once Seeed exposes API
  return false;
}

// ========== TUNER ==========
float estimateFrequency(const int16_t* buffer, int length, float sampleRate) {
  int zeroCrossings = 0;
  for (int i = 1; i < length; ++i) {
    if ((buffer[i - 1] < 0 && buffer[i] >= 0) || (buffer[i - 1] > 0 && buffer[i] <= 0)) {
      zeroCrossings++;
    }
  }
  float cycles = zeroCrossings / 2.0f;
  float duration = length / sampleRate;
  return (duration > 0.0f && cycles > 0.0f) ? cycles / duration : 0.0f;
}

float computeCentsDifference(float measuredFreq, float targetFreq) {
  return (measuredFreq > 0.0f && targetFreq > 0.0f)
    ? 1200.0f * log(measuredFreq / targetFreq) / log(2.0f)
    : 0.0f;
}

// ========== LED ==========
void updateTuningLeds(float cents) {
  digitalWrite(LED_FLAT_PIN,   cents < -CENTER_TOLERANCE_CENTS);
  digitalWrite(LED_CENTER_PIN, abs(cents) <= CENTER_TOLERANCE_CENTS);
  digitalWrite(LED_SHARP_PIN,  cents > CENTER_TOLERANCE_CENTS);
}

// ========== DISPLAY ==========
void showError(const char* msg) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("Error:");
  display.println(msg);
  display.display();
}

void updateTunerDisplay(float freq, float cents, AudioSource src) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 0);

  display.print("Src: ");
  display.println((src == AudioSource::JackAnalog) ? "Jack" : "Mic");

  display.print("Freq: ");
  display.print(freq, 1);
  display.println(" Hz");

  display.print("Target: ");
  display.print(targetFrequency, 1);
  display.println(" Hz");

  display.print("Cents: ");
  display.print(cents, 1);

  int centerX = SCREEN_WIDTH / 2;
  int centerY = 40;
  int barLength = 40;
  float maxCents = 50.0f;
  float norm = constrain(cents / maxCents, -1.0f, 1.0f);
  int barX = centerX + (int)(norm * (barLength / 2));

  display.drawLine(centerX, centerY - 5, centerX, centerY + 5, SSD1306_WHITE);
  display.drawLine(barX, centerY - 8, barX, centerY + 8, SSD1306_WHITE);
  display.display();
}
