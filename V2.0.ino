/////////// includes ////////////////////
#include <Arduino.h>
#include <TM1637Display.h>
#include <TCA6424A.h>
#include <EEPROM.h>

#define DRV8840_I0 0
#define DRV8840_I1 1
#define DRV8840_I2 2
#define DRV8840_I3 3
#define DRV8840_I4 4

#define DRV8840_EN 5
#define DRV8840_DIR 6
#define DRV8840_nSLEEP 7

#define DRV8840_nRESET 8
#define DRV8840_nDECAY 9

/// Active Low LED
#define LED5_ERROR 10
#define LED4_EMPTY 11
#define LED3_HOME 12
#define LED2_PAUSE 13
#define LED1_READY 14

// Active High Ios
#define RELAY 15
#define BUZZER_24V 16
#define BUZZER_5V 17

/// Active Low Open Drain
#define BLDC_OUT1 18
#define BLDC_OUT2 19
#define BLDC_OUT3 20
#define BLDC_OUT4 21

#define BTS_DIR 18  /// GPIO1
#define BTS_EN 23   /// GPIO6

#define CLK 19  // TM1637 CLOCK
#define DIO 18  // TM1637 DATA

#define BUTTON_UP 32
#define BUTTON_DOWN 33
#define BUTTON_STSP 27
#define DRV8840_nFAULT 34
#define IR_IN 26         /// ADC2_CH9
#define BLDC_IN1 39      // ESP IO IN (EXTERNAL PULLUP)
#define BLDC_IN2 36      // ESP IO IN (EXTERNAL PULLUP)
#define TRACK_SENSOR 39  // ESP IO IN (EXTERNAL PULLUP)

//////////////////// objects //////////////////////
TM1637Display display(CLK, DIO);
TCA6424A myExpander;


///////////////////// VRs ////////////////////
hw_timer_t *timer1 = NULL;
hw_timer_t *timer2 = NULL;
const int timerInterval = 1000000;  // 1 second interval in microseconds
bool stsp_flag = 0;
volatile bool home_timer_flag = 0;

uint8_t bldc_run_before_home_delay = 10;  ///eeprom_0  /// delay in seconds
uint8_t grain_level = 5;                  ///eeprom_0  /// delay in seconds
uint8_t current_screen = 1;
uint8_t trq_level = 10;
uint8_t trq_level_base = 25;
uint8_t grain_level_m_factor = 11;  /// 1 =100ms
uint8_t stop_time = 30;             /// in seconds
bool start_stop_flag = false;
volatile uint8_t stop_time_counter = 0;  /// in seconds
bool no_grain_flag = false;

const uint8_t SEG_DONE[] = {
  SEG_B | SEG_C | SEG_D | SEG_E | SEG_G,          // d
  SEG_A | SEG_B | SEG_C | SEG_D | SEG_E | SEG_F,  // O
  SEG_C | SEG_E | SEG_G,                          // n
  SEG_A | SEG_D | SEG_E | SEG_F | SEG_G           // E
};

///////////////////////////////// extra functions  /////////////////////

void IRAM_ATTR homeTimer() {
  home_timer_flag = !home_timer_flag;
}
void home_timer_init() {
  // Set up and start the timer
  timer1 = timerBegin(0, 80, true);                // Timer 0, 80 prescaler, true for count up
  timerAttachInterrupt(timer1, &homeTimer, true);  // Attach the interrupt
  timerAlarmWrite(timer1, timerInterval, true);    // Set the alarm value and auto-reload
  // timerAlarmEnable(timer1);                        // Enable the alarm
}

void IRAM_ATTR no_grain_ISR() {
  stop_time_counter++;
}
void no_grain_timer_init() {
  // Set up and start the timer
  timer2 = timerBegin(0, 80, true);                   // Timer 0, 80 prescaler, true for count up
  // timerAttachInterrupt(timer2, &no_grain_ISR, true);  // Attach the interrupt
  timerAlarmWrite(timer2, timerInterval, true);       // Set the alarm value and auto-reload
  timerAlarmEnable(timer2);                           // Enable the alarm
}


void IO_init() {
  Serial.begin(115200);
  delay(5);
  Wire.begin();  /// Expander IC
  myExpander.initialize();
  bool test_ct = myExpander.testConnection();
  if (test_ct) {
    Serial.println("Expander init - done!");
  } else {
    Serial.println("Expander NOT working!");
  }
  /// input config
  pinMode(BUTTON_UP, INPUT_PULLUP);
  pinMode(BUTTON_DOWN, INPUT_PULLUP);
  pinMode(BUTTON_STSP, INPUT_PULLUP);
  // pinMode(IR_IN, INPUT_PULLUP);
  pinMode(BLDC_IN1, INPUT_PULLUP);
  pinMode(BLDC_IN2, INPUT_PULLUP);
  pinMode(DRV8840_nFAULT, INPUT_PULLUP);
  /// output config
  myExpander.setAllDirection(0, 0, 0);  /// all bank as output   /// default value.
  myExpander.writeBank(0, 0);           /// DRV8840 io SET TO 0

  myExpander.writePin(LED1_READY, 1);
  myExpander.writePin(LED2_PAUSE, 1);
  myExpander.writePin(LED3_HOME, 1);
  myExpander.writePin(LED4_EMPTY, 1);
  myExpander.writePin(LED5_ERROR, 1);

  myExpander.writePin(RELAY, 0);
  myExpander.writePin(BUZZER_24V, 0);
  myExpander.writePin(BUZZER_5V, 0);
  myExpander.writePin(BLDC_OUT4, 1);

  myExpander.writePin(BTS_DIR, 0);
  myExpander.writePin(BTS_EN, 0);

  //// TM1637 init
  display_init();
}


//############################################# setup ####################################
void setup() {
  delay(200);  /// debugging delay
  IO_init();
  EEPROM.begin(512);  // eeprom begin with 512 bytes
  // eeprom_write();     /// write default values
  eeprom_read_default();
  // home_timer_init();  /// enable home timer for buzzer & display.
  no_grain_timer_init();
  // drv8840_init();  ///
  home();
}

//############################################# LOOP ######################################
void loop() {

  if (digitalRead(BUTTON_STSP) == LOW) {  /// MENU BUTTON
    unsigned long ctd = millis();
    while (digitalRead(BUTTON_STSP) == LOW) {
    }

    if (millis() - ctd > 2000) {
      start_stop_flag = !start_stop_flag;
      Serial.println("***Flour Mill - ON/OFF***");
    } else {
      current_screen += 1;
    }

    if (current_screen > 5) {
      current_screen = 1;
    }
    switch (current_screen) {
      case 1:
        update_display(grain_level);
        Serial.printf("Current Screen %d \n", current_screen);
        break;
      case 2:
        update_display(bldc_run_before_home_delay);
        Serial.printf("Current Screen %d \n", current_screen);
        break;
      case 3:
        update_display(trq_level);
        Serial.printf("Current Screen %d \n", current_screen);
        break;
      case 4:
        update_display(grain_level_m_factor);
        Serial.printf("Current Screen %d \n", current_screen);
        break;
      case 5:
        update_display(stop_time);
        Serial.printf("Current Screen %d \n", current_screen);
        break;
      default:
        break;
    }
  }

  if (digitalRead(BUTTON_UP) == LOW) {  /// UP BUTTON
    switch (current_screen) {
      case 1:
        grain_level++;
        if (grain_level > 100) {
          grain_level = 100;
        } else {
          Serial.printf("Grain Level = %d \n", grain_level);
          update_display(grain_level);
          eeprom_update(current_screen);
          home_motor_up();
          delay(grain_level_m_factor * 100);
          home_motor_stop();
        }
        break;
      case 2:
        bldc_run_before_home_delay++;
        if (bldc_run_before_home_delay > 250) {
          bldc_run_before_home_delay = 250;
        }
        update_display(bldc_run_before_home_delay);
        eeprom_update(current_screen);
        Serial.printf("bldc_run_before_home_delay = %d \n", bldc_run_before_home_delay);
        break;
      case 3:
        trq_level++;
        if (trq_level > 31) {
          trq_level = 31;
        }
        update_display(trq_level);
        eeprom_update(current_screen);
        Serial.printf("trq_level = %d \n", trq_level);
        break;
      case 4:
        grain_level_m_factor++;
        if (grain_level_m_factor > 250) {
          trq_level = 250;
        }
        update_display(grain_level_m_factor);
        eeprom_update(current_screen);
        Serial.printf("grain_level_m_factor = %d \n", grain_level_m_factor);
        break;
      case 5:
        stop_time++;
        if (stop_time > 250) {
          stop_time = 250;
        }
        update_display(stop_time);
        eeprom_update(current_screen);
        Serial.printf("stop_time = %d \n", stop_time);
        break;
      default:
        break;
    }
  }

  if (digitalRead(BUTTON_DOWN) == LOW) {  /// down BUTTON
    switch (current_screen) {
      case 1:
        grain_level--;
        if (grain_level < 0) {
          grain_level = 0;
        } else {
          Serial.printf("grain_level = %d \n", grain_level);
          update_display(grain_level);
          eeprom_update(current_screen);
          home_motor_dwon();
          delay(grain_level_m_factor * 100);
          home_motor_stop();
        }
        break;
      case 2:
        bldc_run_before_home_delay--;
        if (bldc_run_before_home_delay < 0) {
          bldc_run_before_home_delay = 0;
        }
        update_display(bldc_run_before_home_delay);
        eeprom_update(current_screen);
        Serial.printf("bldc_run_before_home_delay = %d \n", bldc_run_before_home_delay);
        break;
      case 3:
        trq_level--;
        if (trq_level < 0) {
          trq_level = 0;
        }
        update_display(trq_level);
        eeprom_update(current_screen);
        Serial.printf("trq_level = %d \n", trq_level);
        break;
      case 4:
        grain_level_m_factor--;
        if (grain_level_m_factor < 0) {
          grain_level_m_factor = 0;
        }
        update_display(grain_level_m_factor);
        eeprom_update(current_screen);
        Serial.printf("grain_level_m_factor = %d \n", grain_level_m_factor);
        break;
      case 5:
        stop_time--;
        if (stop_time < 0) {
          stop_time = 0;
        }
        update_display(stop_time);
        eeprom_update(current_screen);
        Serial.printf("stop_time = %d \n", stop_time);
        break;
      default:
        break;
    }
  }
  ///// start - stop
  if (start_stop_flag) {             /// start
    if (!digitalRead(TRACK_SENSOR))  //  sensor is active low | GRAIN DETECTED
    {
      myExpander.writePin(BLDC_OUT4, 0);   /// active low - Run the BLED Motor
      myExpander.writePin(RELAY, 1);       /// relay on - motor on.
      myExpander.writePin(LED4_EMPTY, 1);  // off
      myExpander.writePin(LED3_HOME, 1);   // off
      myExpander.writePin(LED1_READY, 1);  // off
      myExpander.writePin(LED2_PAUSE, 1);  // off

    } else {
      start_stop_flag = false;
      no_grain_flag = true;
      myExpander.writePin(LED4_EMPTY, 0);  // on
      timerAttachInterrupt(timer2, &no_grain_ISR, true);  // Attach the interrupt
      Serial.println("No Grain - Stop Timer Start");
    }

  } else {  /// stop
    if (no_grain_flag) {
      // Serial.printf("No Grain Counter = %d", stop_time_counter);
      if (stop_time_counter >= stop_time) {
        myExpander.writePin(BLDC_OUT4, 1);  /// active low - stop the motor
        myExpander.writePin(RELAY, 0);      /// relay off - motor off.
        no_grain_flag = false;
        timerDetachInterrupt(timer2);  // Attach the interrupt
        Serial.println("Flour Mill - OFF (No Grain Timout)");
        myExpander.writePin(LED2_PAUSE, 0);  // off
        stop_time_counter = 0;
      }

    } else {
      myExpander.writePin(BLDC_OUT4, 1);  /// active low - stop the motor
      myExpander.writePin(RELAY, 0);      /// relay off - motor off.
    }
  }
}

//############################################# EXTRA FUNCTIONS ############################

void display_init() {
  display.setBrightness(0x0f);
  display.clear();
  uint8_t data[] = { 0xff, 0xff, 0, 0 };
  data[0] = display.encodeDigit(0);
  data[1] = display.encodeDigit(0);
  display.setSegments(data);
  delay(50);
}

void update_display(uint16_t ct) {

  uint8_t data[] = { 0xff, 0xff, 0, 0 };
  data[0] = display.encodeDigit(ct / 10);
  data[1] = display.encodeDigit(ct % 10);
  display.setSegments(data);
  delay(500);
}

void eeprom_update(uint8_t screen_no) {
  switch (screen_no) {
    case 1:
      EEPROM.write(1, grain_level);
      EEPROM.commit();
      delay(1);
      break;
    case 2:
      EEPROM.write(2, bldc_run_before_home_delay);
      EEPROM.commit();
      delay(1);
      break;
    case 3:
      EEPROM.write(3, trq_level);
      EEPROM.commit();
      delay(1);
      break;
    case 4:
      EEPROM.write(4, grain_level_m_factor);
      EEPROM.commit();
      delay(1);
      break;
    case 5:
      EEPROM.write(5, stop_time);
      EEPROM.commit();
      delay(1);
      break;
    default:
      break;
  }
}

void eeprom_write() {
  EEPROM.write(1, grain_level);
  EEPROM.write(2, bldc_run_before_home_delay);
  EEPROM.write(3, trq_level);
  EEPROM.write(4, grain_level_m_factor);
  EEPROM.write(5, stop_time);

  EEPROM.commit();
  delay(1);
}

void eeprom_read_default() {
  grain_level = EEPROM.read(1);
  bldc_run_before_home_delay = EEPROM.read(2);
  trq_level = EEPROM.read(3);
  grain_level_m_factor = EEPROM.read(4);
  stop_time = EEPROM.read(5);
}

void home_motor_up() {
  myExpander.writePin(BTS_DIR, 1);
  myExpander.writePin(BTS_EN, 0);
}

void home_motor_dwon() {
  myExpander.writePin(BTS_DIR, 0);
  myExpander.writePin(BTS_EN, 1);
}

void home_motor_stop() {
  myExpander.writePin(BTS_EN, 0);
  myExpander.writePin(BTS_DIR, 0);
}

///////////////////////////// homming ////////////////////
void home() {

  Serial.println("IN HOMING");
  //step 1 : BLDC ON For serverl seconds
  myExpander.writePin(BLDC_OUT4, 0);  /// active low
  Serial.println("RELAY ON");
  unsigned long ct = millis();
  uint8_t data[] = { 0xff, 0xff, 0, 0 };
  int adc;
  float t_limit;
  bool invt_flag = true;
  do {
    if (invt_flag) {
      myExpander.writePin(LED3_HOME, 0);   // on
      myExpander.writePin(BUZZER_24V, 1);  /// buzzer on
      display.setBrightness(0x0f);
      data[0] = display.encodeDigit(0);
      data[1] = display.encodeDigit(0);
      display.setSegments(data);
    } else {
      myExpander.writePin(LED3_HOME, 1);   /// led off
      myExpander.writePin(BUZZER_24V, 0);  /// buzzer off
      display.clear();                     /// disply off
    }
    delay(1000);
    invt_flag = !invt_flag;
    Serial.printf("millis() = %d", millis());
  } while ((bldc_run_before_home_delay * 1000) > (millis() - ct));
  myExpander.writePin(BLDC_OUT4, 1);  /// BLDC Relay off
  Serial.println("RELAY OFF");

  // step 2 : do homing
  Serial.println("Current Loop Measure");
  home_motor_dwon();  /// down towards homing
  delay(400);
  do {
    adc = analogRead(26);
    delay(200);
    t_limit = ((trq_level_base + (float)trq_level / 2) * 100);
  } while (t_limit > adc);
  home_motor_stop();
  myExpander.writePin(LED3_HOME, 0);   /// led on
  myExpander.writePin(BUZZER_24V, 0);  /// buzzer off
  display.setBrightness(0x0f);
  data[0] = display.encodeDigit(0);
  data[1] = display.encodeDigit(0);
  display.setSegments(data);
  delay(2000);
  Serial.println("Moving Motor Up");
  //// home motor up to original position
  home_motor_up();  //motor up towards the initial position
  ct = millis();
  while ((grain_level * grain_level_m_factor * 100) > (millis() - ct)) {
    //wait.
  }
  home_motor_stop();
  myExpander.writePin(LED3_HOME, 1);   // off
  myExpander.writePin(BUZZER_24V, 0);  /// buzzer off
  update_display(grain_level);
  myExpander.writePin(LED1_READY, 0);  // on
  Serial.println("**Homing Done**");
}


// /////////////////////////////////////////////////////////
// void drv8840_init() {
//   //1.5 / 5 * 0.1 = 3A
//   //0x01 = 5% Currnt
//   //0x07 = 34% currnt  0 0111
//   //0x0B = 51% currnt  0 1100

//   // dacWrite(25, 128);  /// 3.3 V for DRV8840 REF.

//   myExpander.writePin(DRV8840_I0, 1);
//   myExpander.writePin(DRV8840_I1, 1);
//   myExpander.writePin(DRV8840_I2, 1);
//   myExpander.writePin(DRV8840_I3, 1);
//   myExpander.writePin(DRV8840_I4, 1);

//   myExpander.writePin(DRV8840_nSLEEP, 1);
//   // myExpander.writePin(DRV8840_nDECAY, 1);

//   myExpander.writePin(DRV8840_nRESET, 0);
//   delay(1000);
//   myExpander.writePin(DRV8840_nRESET, 1);
//   delay(50);
// }



// void testDisplay() {
//   display.setBrightness(0x0f);
//   display.clear();
//   uint8_t data[] = { 0xff, 0xff, 0, 0 };
//   // uint8_t ct = 0;
//   for (uint8_t ct = 0; ct <= 99; ct++) {
//     data[0] = display.encodeDigit(ct / 10);
//     data[1] = display.encodeDigit(ct % 10);
//     display.setSegments(data);
//     myExpander.writePin(14 - ct, 0);
//     delay(2000);
//     myExpander.writePin(14 - ct, 1);
//   }
//   // display.setSegments(SEG_DONE);
//   // uint8_t blank[] = { 0x00, 0x00, 0x00, 0x00 };
// }


// int16_t cnt = 0;

// while (1) {
//   if (digitalRead(BUTTON_STSP) == 0) {
//     stsp_flag = !stsp_flag;
//     delay(1000);
//   } else if ((digitalRead(BUTTON_UP) == 0) && (stsp_flag == 1)) {
//     update_display(cnt++);
//   } else if ((digitalRead(BUTTON_DOWN) == 0) && (stsp_flag == 1)) {
//     update_display(cnt--);
//   }
// }


// // Show decimal numbers with/without leading zeros
// display.showNumberDec(0, false);  // Expect: ___0
// delay(TEST_DELAY);
// display.showNumberDec(0, true);  // Expect: 0000
// delay(TEST_DELAY);
// display.showNumberDec(1, false);  // Expect: ___1
// delay(TEST_DELAY);
// display.showNumberDec(1, true);  // Expect: 0001
// delay(TEST_DELAY);
// display.showNumberDec(301, false);  // Expect: _301
// delay(TEST_DELAY);
// display.showNumberDec(301, true);  // Expect: 0301
// delay(TEST_DELAY);
// display.clear();
// display.showNumberDec(14, false, 2, 1);  // Expect: _14_
// delay(TEST_DELAY);
// display.clear();
// display.showNumberDec(4, true, 2, 2);  // Expect: 04__
// delay(TEST_DELAY);
// display.showNumberDec(-1, false);  // Expect: __-1
// delay(TEST_DELAY);
// display.showNumberDec(-12);  // Expect: _-12
// delay(TEST_DELAY);
// display.showNumberDec(-999);  // Expect: -999
// delay(TEST_DELAY);
// display.clear();
// display.showNumberDec(-5, false, 3, 0);  // Expect: _-5_
// delay(TEST_DELAY);
// display.showNumberHexEx(0xf1af);  // Expect: f1Af
// delay(TEST_DELAY);
// display.showNumberHexEx(0x2c);  // Expect: __2C
// delay(TEST_DELAY);
// display.showNumberHexEx(0xd1, 0, true);  // Expect: 00d1
// delay(TEST_DELAY);
// display.clear();
// display.showNumberHexEx(0xd1, 0, true, 2);  // Expect: d1__
// delay(TEST_DELAY);




// while (1) {
//   if (Serial.available() > 0) {
//     char dt = Serial.read();
//     if (dt == 'f') {
//       myExpander.writePin(DRV8840_DIR, 0);  /// SET DIR DOWN
//       myExpander.writePin(DRV8840_EN, 1);   ///  ON THE MOTOR
//       Serial.println("Motor Forward");
//     } else if (dt == 'r') {
//       myExpander.writePin(DRV8840_DIR, 1);  /// SET DIR DOWN
//       myExpander.writePin(DRV8840_EN, 1);   ///  ON THE MOTOR
//       Serial.println("Motor Reverse");
//     } else if (dt == 's') {
//       // myExpander.writePin(DRV8840_DIR, 1);  /// SET DIR DOWN
//       myExpander.writePin(DRV8840_EN, 0);  ///  ON THE MOTOR
//       Serial.println("Motor Stop");
//     }
//   }
//   delay(500);
//   int adc = analogRead(26);
//   Serial.println(adc);
// }