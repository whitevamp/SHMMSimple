// SCULLCOM HOBBY ELECTRONICS
// MILLIVOLT METER USING LTC2400 24bit ADC CHIP
// Software version 7.00
// 4.096 volt precision reference (ADR4540)

// --- Library includes -----------------------------------------------------------------------

#include <avr/eeprom.h>                       // EEPROM Library used to store calibration data
#include <SPI.h>                              // SPI Library used to communicate with LTC2400
#include <LiquidCrystal.h>                    // LCD Library, well, for the LCD :)


// --- PIN assignments ------------------------------------------------------------------------

#define LCD_RS       8
#define LCD_ENABLE   7
#define LCD_D4       6
#define LCD_D5       5
#define LCD_D6       4
#define LCD_D7       3
// LCD_R/W pin connected to ground

#define LTC2400_SCK 13
#define LTC2400_SDO 12
#define LTC2400_CS  10

#define BUTTON_CAL   2                        // Button has a pull-down on board; active HIGH
#define BUTTON_DEC   9                        // Uses internal pull-up; active LOW


// --- Constants ------------------------------------------------------------------------------

#define NUMBER_OF_SAMPLES           5         // Number of samples to average
#define VOLTAGE_REFERENCE           4.096

#define EEPROM_CALIBRATION_ADDRESS  0         // EEPROM offset for calibration data

#define MAIN_LOOP_DELAY_MS          20
#define DEBOUNCE_DELAY_MS           150
#define INTRO_DELAY_MS              3000
#define CALIBRATION_PROMPT_DELAY_MS 3000


// --- Global variables -----------------------------------------------------------------------

LiquidCrystal g_lcd(LCD_RS, LCD_ENABLE, LCD_D4, LCD_D5, LCD_D6, LCD_D7);

uint32_t g_calibration_offset     = 0;
uint32_t g_samples[NUMBER_OF_SAMPLES] = {0};
uint32_t g_current_sample = 0;
uint8_t g_number_of_decimals  = 6;


// --- Setup function (only runs once) --------------------------------------------------------

void setup(void) {
  setupIOPins();
  setupSPIBus();
  setupLCD();

  showIntro();

  loadCalibrationData();
  showCalibrationData();

  showHeader();
  startADC();
}


// --- Main loop ------------------------------------------------------------------------------

void loop(void) {
  if (digitalRead(BUTTON_CAL) == HIGH) {      // Pull-down on board; active HIGH
    performCalibration();
    showHeader();                             // Back to normal operation

  } else if (digitalRead(BUTTON_DEC) == LOW) { // Internal pull-up used; active LOW
    adjustDecimalPlaces();
    showHeader();                             // Back to normal operation

  } else {
    readADC();
    showReading();
  }

  delay(MAIN_LOOP_DELAY_MS);
}


// --- Various setup helpers functions --------------------------------------------------------

void setupIOPins(void) {
  pinMode(BUTTON_CAL, INPUT);
  pinMode(BUTTON_DEC, INPUT_PULLUP);
  pinMode(LTC2400_CS, OUTPUT);
  digitalWrite(LTC2400_CS, HIGH);             // Setting chips select HIGH disables ADC initially
}

void setupSPIBus(void) {
  SPI.begin();
  SPI.setBitOrder(MSBFIRST);
  SPI.setDataMode(SPI_MODE0);                 // Mode 0 (MOSI read on rising edge (CPLI=0) and SCK idle low (CPOL=0))
  SPI.setClockDivider(SPI_CLOCK_DIV16);       // Divide Arduino clock by 16 to give a 1 MHz SPI clock
}

void setupLCD(void) {
  g_lcd.begin(16, 2);
}


// --- ADC helper functions -------------------------------------------------------------------

float convertToVoltage(uint32_t reading) {
  return reading * 10 * VOLTAGE_REFERENCE / 16777216;
}

void startADC(void) {
  // Start conversion on the LTC2400
  // The ADC will be in free-running mode and continue conversion until
  // CS is pulled high again, which we're not going to do :)
  digitalWrite(LTC2400_CS, LOW);
}

bool readADC(void) {
  if (digitalRead(LTC2400_SDO))               // SDO will indicate end-of-conversion (EOC) by going low when it's done
    return false;                             // Return immediate if result is not ready...

  uint32_t reading = 0;
  for (int i = 0; i < 4; ++i) {               // Ready 4 bytes (32 bits) from the ADC
    reading <<= 8;                            // Before each readin shift the existing content over to make room
    reading |= SPI.transfer(0xFF);            // Read one byte
    if (i == 0)
      reading &= 0x0F;                        // Discard 4 status bits of the first byte
  }

  reading >>= 4;                              // Discard 4 left most sub LSB bits

  g_samples[g_current_sample++] = reading;    // Store value in the bucket used to calculate averages
  if (g_current_sample == NUMBER_OF_SAMPLES)  // Jump back to the first slot once we go past the last slot
    g_current_sample = 0;

  return true;
}

uint32_t getADCAverage(void) {
  uint32_t sum = 0;
  for (int i = 0; i < NUMBER_OF_SAMPLES; i++)
    sum += g_samples[i];                      // Sum of all stored up samples

  sum = sum / NUMBER_OF_SAMPLES;              // Calculate average by dividing total readings value by number of samples taken
  sum = sum - g_calibration_offset;           // Subtract calibration offset

  return sum;
}

void showReading(void) {
  uint32_t reading = getADCAverage();
  float volt = convertToVoltage(reading);

  char prefix = 0;
  if (volt < 0.001) {
    volt = volt * 1000000;
    prefix = 'u';

  } else if (volt < 1) {
    volt = volt * 1000;
    prefix = 'm';
  }

  g_lcd.setCursor(0, 1);                      // Jump to second line in the display (bottom)
  g_lcd.print(volt, g_number_of_decimals);    // Print voltage as floating number w/ the right number of decimal places
  g_lcd.print(" ");                           // Add one blank space after voltage reading

  if (prefix)
    g_lcd.print(prefix);
  g_lcd.print("V    ");                       // Extra spaces to clean up when voltages go from large to small

  g_lcd.setCursor(15,1);
  g_lcd.print(g_number_of_decimals);
}

uint32_t performCalibration(void) {
  showCalibrationPrompt();
  g_lcd.setCursor(11, 0);

  g_calibration_offset = 0;                   // Reset calibration
  for (int i = 0; i < NUMBER_OF_SAMPLES; )    // Get multiple measurements
  {
    if (readADC()) {                          // Wait for the ADC measurements to be ready
      ++i;
      g_lcd.print('.');
    }
  }
  g_calibration_offset = getADCAverage();     // Read new average value

  saveCalibrationData();
  showCalibrationData();
}


// --- UI helper functions --------------------------------------------------------------------

void adjustDecimalPlaces(void) {
  ++g_number_of_decimals;
  if (g_number_of_decimals > 6)
    g_number_of_decimals = 0;
    
  delay(DEBOUNCE_DELAY_MS);                   // Very simple de-bounce delay
}

void showIntro(void) {
  g_lcd.clear();
  g_lcd.setCursor(4, 0);
  g_lcd.print("SCULLCOM");
  g_lcd.setCursor(0, 1);
  g_lcd.print("Hobby Electronic");
  delay(INTRO_DELAY_MS);
}

void showCalibrationData(void) {
  g_lcd.clear();
  g_lcd.setCursor(0, 0);
  g_lcd.print("Adjust Factor");
  g_lcd.setCursor(0, 1);
  g_lcd.print(g_calibration_offset);
  delay(CALIBRATION_PROMPT_DELAY_MS);
}

void showCalibrationPrompt(void) {
  g_lcd.clear();
  g_lcd.setCursor(0, 0);
  g_lcd.print("Calibration");
  g_lcd.setCursor(0, 1);
  g_lcd.print("Short input lead");
  delay(CALIBRATION_PROMPT_DELAY_MS);
}

void showHeader(void) {
  g_lcd.clear();
  g_lcd.setCursor(0, 0);
  g_lcd.print("Millivolt Meter");
}


// --- EEPROM helper functions ----------------------------------------------------------------

void loadCalibrationData(void) {
  g_calibration_offset = eeprom_read_dword(EEPROM_CALIBRATION_ADDRESS);
}

void saveCalibrationData(void) {
  eeprom_write_dword(EEPROM_CALIBRATION_ADDRESS, g_calibration_offset);
}
