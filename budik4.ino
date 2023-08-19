#include <SPI.h>
#include "LowPower.h"
#include "U8glib.h"
#include <RotaryEncoder.h>
#include <Wire.h>
#include "RTClib.h"
#include "PinChangeInterrupt.h"


// Encoder

#define ENC_CLK 10
#define ENC_DT 11
#define ENC_SW 12  // Button
static int encLastPosition = 0;
static int encCurrentPosition = 0;
static int encLastButtonPosition = 0;
static int encCurrentButtonPosition = 0;
static int encCurrentRotationMillis = 0;
static int encLastRotationMillis = 0;

static int isTick = 0;
RotaryEncoder *encoder = nullptr;


// Buzzer

#define BEEP 4
boolean isBuzzerShortBeep = false;
unsigned long buzzerAlarmBeepTimer = 0;

// I2C

#define I2C_SUPPLY_PIN 8


// I2C Display

#define DISPLAY_UPD_MILLIS 250
U8GLIB_SSD1306_128X32 u8g;


// I2C RTC

#define RTC_UPD_MILLIS 250
RTC_DS1307 clock;
DateTime clockLastReadingTime;
DateTime clockCurrentReadingTime;
int clockIncorrectReadingCount = 0;  // I'm suspicious that on battery it can get stucked


// Alarm and Time

// #define TIME_SET_HOLD_BUTTON_MS 5000  // Hold button to set clock
#define ALARM_SET_HOLD_BUTTON_MS 3000
bool isButtonHoldStart = false;
bool isButtonHoldPersisting = false;
unsigned long buttonHoldPersistingTimer = 0;

bool isTimeSetActive = false;
bool isAlarmTimeSetActive = false;
bool isAlarmActivated = false;
bool isAlarmTriggered = false;

DateTime alarmTime = DateTime(2023, 0, 0, 0, 0, 0);


// Sleep

#define SLEEP_WAKEUP_TIME_MS 15000  // Must be at least 5ms so it will skip sleep
unsigned long sleepWakeupTimer = 0;


// Alarm

enum alarmStatusEnum {
  NOT_SET,
  POSTPONED,
  BEEPING
};

int alarm0[] = { 12, 0 };
alarmStatusEnum alarmStatus = NOT_SET;


void setup() {
  // Serial

  Serial.begin(115200);
  Serial.println("Alarm clock");


  // Encoder

  encoder = new RotaryEncoder(ENC_CLK, ENC_DT, RotaryEncoder::LatchMode::TWO03);

  pinMode(ENC_SW, INPUT_PULLUP);   // button
  pinMode(ENC_CLK, INPUT_PULLUP);  // When no-pullup is set then it drains 1mA instead 0f 0.2mA. I don't know why.
  pinMode(ENC_DT, INPUT_PULLUP);
  attachPCINT(digitalPinToPCINT(ENC_SW), onEncoderTurned, CHANGE);
  attachPCINT(digitalPinToPCINT(ENC_CLK), onEncoderTurned, CHANGE);
  attachPCINT(digitalPinToPCINT(ENC_DT), onEncoderTurned, CHANGE);


  // Buzzer

  pinMode(BEEP, OUTPUT);


  // I2C

  pinMode(I2C_SUPPLY_PIN, OUTPUT);
  digitalWrite(I2C_SUPPLY_PIN, HIGH);


  // I2C Display

  u8g = U8GLIB_SSD1306_128X32(U8G_I2C_OPT_NONE);
  // Je to potreba?
  if (u8g.getMode() == U8G_MODE_R3G3B2)
    u8g.setColorIndex(255);  // white
  else if (u8g.getMode() == U8G_MODE_GRAY2BIT)
    u8g.setColorIndex(3);  // max intensity
  else if (u8g.getMode() == U8G_MODE_BW)
    u8g.setColorIndex(1);  // pixel on


  // I2C RTC

  clock.begin();
  clockLastReadingTime = clock.now();
  
  
  // Alarm and time
  
  clock.adjust(DateTime(__DATE__, __TIME__));  // Set current computer time
  alarmTime = DateTime(__DATE__, __TIME__);
}


void onEncoderTurned(void) {
  encoder->tick();
  isTick = 1;
}


int timer = millis();
unsigned int loopCount = 0;

void loop() {

  // Encored

  if (isTick) {
    encCurrentPosition = encoder->getPosition();
    encCurrentButtonPosition = !digitalRead(ENC_SW);
    encCurrentRotationMillis = encoder->getMillisBetweenRotations();
    isTick = 0;  // Here can be concurrency issue with interrupt, but it is so rare
    Serial.println("Button");
  }


  // Buzzer

  // if (isAlarmTriggered && millis() - buzzerAlarmBeepTimer > 1000) {
  //   tone(BEEP, 20);
  //   buzzerAlarmBeepTimer = millis();
  // } 
  // else {
  //   noTone(BEEP);  
  // }
  
  // if(!isAlarmTriggered) {
  //   buzzerAlarmBeepTimer = millis();
  // }


  // Sleep

  bool isReasonToNotSleep = encCurrentPosition != encLastPosition || encCurrentButtonPosition != encLastButtonPosition || isAlarmTriggered;

  if (isReasonToNotSleep) {
    sleepWakeupTimer = millis();  // reset wakeup timer
  }

  bool isSleepAgain = millis() - sleepWakeupTimer > SLEEP_WAKEUP_TIME_MS;

  if (isSleepAgain) {
    Serial.println("Power off");
    // Off
    digitalWrite(I2C_SUPPLY_PIN, LOW);
    LowPower.powerDown(SLEEP_4S, ADC_OFF, BOD_OFF);
    delay(20);  // Rathr add delay because I think that RTC has problem sometimes
    // Full wakeup
    digitalWrite(I2C_SUPPLY_PIN, HIGH);
    clock.begin();
    u8g.begin();

    return;  // To encoder
  }


  // I2C RTC

  if (loopCount % 500 == 0) {
    DateTime read = clock.now();
    boolean isIncorrect = abs(read.unixtime() - clockCurrentReadingTime.unixtime()) > 3600;
    if (isIncorrect && clockIncorrectReadingCount < 5) {  // Otherwise let's print incorrect value to display too see that RTC is screwed up
      clockIncorrectReadingCount++;
      return;
    }
    clockIncorrectReadingCount = 0;
    clockCurrentReadingTime = read;
  }


  // Time set



  // isButtonHoldStart = encCurrentButtonPosition == 1 && encLastButtonPosition == 0;
  // if (isButtonHoldStart) {
  //   buttonHoldPersistingTimer = millis();  // Set timer
  //   isButtonHoldStart = false;
  //   isButtonHoldPersisting = true;
  // } else if (encCurrentButtonPosition == 0) {
  //   isButtonHoldPersisting = false;
  // } else if (isButtonHoldPersisting && millis() - buttonHoldPersistingTimer > ALARM_SET_HOLD_BUTTON_MS && millis() - buttonHoldPersistingTimer < TIME_SET_HOLD_BUTTON_MS) {
  //   isAlarmActivated = !isAlarmActivated;  // Activate setted alarm
  //   isBuzzerShortBeep = true;
  // } else if (isButtonHoldPersisting && millis() - buttonHoldPersistingTimer > TIME_SET_HOLD_BUTTON_MS) {
  //   isAlarmActivated = !isAlarmActivated;
  //   isTimeSetActive = true;  // Enter Time editing mode
  //   isBuzzerShortBeep = true;
  // }

  // // Enter Alarm set mode
  // if (isButtonHoldPersisting) {
  //   int diffMin = encCurrentPosition - encLastPosition;
  //   int rotationAvg = (encLastRotationMillis + encCurrentRotationMillis) / 2;
  //   if (rotationAvg < 20) {
  //     diffMin *= 3;
  //   }
  //   if (rotationAvg < 10) {
  //     diffMin *= 6;
  //   }
  //   alarmTime = DateTime(alarmTime.unixtime() - 60 * diffMin);
  // }

  // Alarm and Time

  isTimeSetActive = encCurrentButtonPosition && encCurrentPosition != encLastPosition;
  isAlarmTimeSetActive = !isTimeSetActive && encCurrentPosition != encLastPosition;
  if (isTimeSetActive || (isAlarmTimeSetActive && !isAlarmActivated)) {
    int diffMin = encCurrentPosition - encLastPosition;
    int rotationAvg = (encLastRotationMillis + encCurrentRotationMillis) / 2;
    if (rotationAvg < 20) {
      diffMin *= 3;
    }
    if (rotationAvg < 10) {
      diffMin *= 6;
    }


    // Time

    if (isTimeSetActive) {
      clockCurrentReadingTime = DateTime(clockCurrentReadingTime.unixtime() - 60 * diffMin);
      clock.adjust(clockCurrentReadingTime);
    }


    // Alarm

    else if (isAlarmTimeSetActive) {
      alarmTime = DateTime(alarmTime.unixtime() - 60 * diffMin);
    }
  }


  // Alarm 

  if(!encCurrentButtonPosition) {
    buttonHoldPersistingTimer = millis();
  }
  else if(millis() - buttonHoldPersistingTimer > ALARM_SET_HOLD_BUTTON_MS) {
    isAlarmActivated = !isAlarmActivated;
    buttonHoldPersistingTimer = millis();
    isAlarmTriggered = false;
  }
  
  if(isAlarmActivated && !isAlarmTriggered) {
    isAlarmTriggered = clockCurrentReadingTime.hour() == alarmTime.hour() && clockCurrentReadingTime.minute() == alarmTime.minute();
  }

  if(!isAlarmActivated && isAlarmTriggered) {
    isAlarmTriggered = false;
  }



  // Display

  bool isDisplayUpdate = clockCurrentReadingTime.unixtime() != clockLastReadingTime.unixtime() || isReasonToNotSleep || isButtonHoldPersisting;

  if (isDisplayUpdate) {  // Pozor, otestovat co udela display
    Serial.println("Display update.");
    u8g.firstPage();
    int failsafeCount = 0;
    do {
      String s;
      if (!isAlarmTimeSetActive) {
        u8g.setFont(u8g_font_osr29r);
        u8g.setPrintPos(9, 31);
        s = getTwoDecimalStr(clockCurrentReadingTime.hour()) + getDelimiterOrEmpty(clockCurrentReadingTime.second()) + getTwoDecimalStr(clockCurrentReadingTime.minute());
              if (isAlarmActivated) {
        s += "'";
      }
      } else {
        u8g.setFont(u8g_font_osr21r);
        u8g.setPrintPos(23, 23);
        s = getTwoDecimalStr(alarmTime.hour()) + ":" + getTwoDecimalStr(alarmTime.minute());
      }
      // String s = String(encoder->getMillisBetweenRotations());
      u8g.println(s);
      if(failsafeCount++ > 100) {
          break; // I'm suspicious that Arduino is getting stuck becouse of infinite loop here
      }
    } while (u8g.nextPage());
  }


  // Finalize

  encLastPosition = encCurrentPosition;
  encLastButtonPosition = encCurrentButtonPosition;
  encLastRotationMillis = encCurrentRotationMillis;
  clockLastReadingTime = clockCurrentReadingTime;

  loopCount++;
}

String getTwoDecimalStr(int i) {
  char out[5];
  if (i < 10) {
    snprintf(out, 5, "0%d", i);
  } else {
    snprintf(out, 5, "%d", i);
  }
  Serial.println(i);
  Serial.println(out);
  return out;
}

String getDelimiterOrEmpty(int seconds) {
  if (seconds % 2 == 0) {
    return " ";
  } else {
    return ":";
  }
}
