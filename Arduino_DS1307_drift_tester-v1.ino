/*


Arduino Uno / ATmega328P wiring :
_________________________________

  A5 = PC5 in pin28 => SCL for I2C
  A4 = PC4 in pin27 => SDA for I2C

*/


#include <TimeLib.h>      // https://github.com/PaulStoffregen/Time
#include <DS1307RTC.h>    // https://github.com/PaulStoffregen/DS1307RTC
#include <Wire.h>         // allow to direct communication with DS1307

time_t t;                 // number of second since 1970 
int drift = 0;            // initial value of the time drift

//
// SETUP
//____________________________________________________________________________________________

void setup() {

  Serial.begin(250000);
  Serial.println("start.....\n");
  delay(1000);


// the function to get the time from the RTC DS1307
  setSyncProvider(RTC.get);
  Serial.print("Initial date RTC & time is: ");
  DisplayTime(); Serial.println(); 

  t = now();                  // get the RTC number of seconds of time since 1970
  unsigned long ActualTime = t;
  
// get DS1307 NVRAM data
  Wire.begin();
  const byte driftOffset = 30;
  byte Fcell0 = ReadNVRAM(0);
  byte Fcell1 = ReadNVRAM(1);
  byte Fday    = ReadNVRAM(4);
  byte Fmonth  = ReadNVRAM(3);
  byte Fyear   = ReadNVRAM(2);
  byte Fhour   = ReadNVRAM(5);
  byte Fminute = ReadNVRAM(6);
  byte Fsecond = ReadNVRAM(7);
  drift   = ReadNVRAM(8) - driftOffset;

// calculate the number of days since the drift setting operation
  if((Fcell0 == 1) && (Fcell1 == 1)) {
    setTime(Fhour, Fminute, Fsecond, Fday, Fmonth, Fyear);
    Serial.print("Flagged date and time: ");
    DisplayTime(); Serial.println();
    Serial.print("Drift value: ");
    Serial.println(drift);

    t = now();
    unsigned long FTime = t;
    unsigned int NumberOfDays = (ActualTime - FTime) / 86400;
    Serial.print("Number of days since last drift test: ");
    Serial.println(NumberOfDays);

    ActualTime -= (drift * NumberOfDays);
    setTime(ActualTime);      // update system time
    Serial.print("Corrected date & time is: ");
    DisplayTime(); Serial.println(); 
  }
  else Serial.println("DS1307 not operated for drift calculation.");
  
  Serial.println();
}

//
// LOOP
//____________________________________________________________________________________________

void loop() {


// automatic drift ajust time once per day when running
  static bool onceDay = true;
  if((hour() == 1) && (minute() == 0) && (second() == 0) && onceDay) {
    Serial.println("Drift correction performed."); 
    onceDay = false;
    t = now();
    t -= drift;        // update the number of seconds since 1970...
    setTime(t);                             // update system time
  }   // end of test onceDay
  else if( minute() == 1 ) onceDay = 1;

  Serial.print("Current date & time is: ");
  DisplayTime(); Serial.println();
  
  delay(10000);
}

//============================================================================================
// list of functions
//============================================================================================

//
// ReadNVRAM() : function to read DS1307 NVRAM 56 bytes memory
//____________________________________________________________________________________________

int ReadNVRAM(byte address) {

  // DS1307 addresses
  const uint8_t DS1307_ADDRESS = 0x68;
  const uint8_t DS1307_NVRAM_BASE = 0x08;
  
  Wire.beginTransmission(DS1307_ADDRESS);
  Wire.write(DS1307_NVRAM_BASE + address);
  Wire.endTransmission();
  Wire.requestFrom(DS1307_ADDRESS, (byte) 1);
  return Wire.read();
}

//
// DisplayTime() : display NTP date and time
//____________________________________________________________________________________________

void DisplayTime() {
  Serial.print(day()); Serial.print("/");
  Print2digits(month()); Serial.print("/");
  Serial.print(year());Serial.print(" ");  
  Print2digits(hour()); Serial.print(":");
  Print2digits(minute()); Serial.print(":");
  Print2digits(second());
}

//
// PrintNum() : display number within 2 digits
//____________________________________________________________________________________________

void Print2digits( byte number ) {
  if( number < 10 ) Serial.print("0");
  Serial.print(number);
}
