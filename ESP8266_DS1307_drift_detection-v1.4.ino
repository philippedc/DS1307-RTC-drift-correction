/*
DS1307 drift detection and correction

1- plug the DS1307 to the ESP8266 module,
2- the esp8266 first updates the DS1307, then ESP8266 can be powered off
3- after 24 hours the drift is calculated (should be around few seconds per day)
4- the drift value is stored in the DS1307 NVRAM
5- if available the DS1307 NVRAM is copied into the 24C32 EEPROM

An project idea from:
- NVRAM use : https://www.carnetdumaker.net/articles/utiliser-un-module-horloge-temps-reel-ds1307-avec-une-carte-arduino-genuino/
- 24C32 use : https://lastminuteengineers.com/ds1307-rtc-arduino-tutorial/

_________________________________________________________________
|                                                               |
|       author : Philippe de Craene <dcphilippe@yahoo.fr        |
|       Any feedback is welcome                                 |
                                                                |
_________________________________________________________________

Materials :
 1* Wemos D1 mini - tested with IDE version 1.8.7 and 1.8.9
 1* a RTC 1307 to test for drift correction
 
ESP8266 pinup :

D1 => SCL DS1307
D2 => SDA DS1307)

Versions chronology:
version 1    - 10 nov. 20  - 
version 1.1  - 12 nov. 20  - add the drift in 24C32


*/

#include <ESP8266WiFi.h>       // https://github.com/esp8266/Arduino
#include <WiFiUdp.h>
#include <WiFiManager.h>       // https://github.com/tzapu/WiFiManager
#include <ESP8266WebServer.h>  // required pour WifiManager.h
#include <DNSServer.h>         // required pour WifiManager.h
#include <ArduinoOTA.h>        // https://github.com/marcudanf/arduinoOTA
#include <TimeLib.h>           // https://github.com/PaulStoffregen/Time
#include <DS1307RTC.h>         // https://github.com/PaulStoffregen/DS1307RTC
#include <Wire.h>              // allow to direct communication with DS1307


// NTP server declaration
//---------------------------------
unsigned int localPort = 2390;         // local port to listen for UDP packets
/* Don't hardwire the IP address or we won't get the benefits of the pool.
    Lookup the IP address for the host name instead */
//IPAddress timeServer(129, 6, 15, 28);   // time.nist.gov NTP server
IPAddress timeServerIP;                 // time.nist.gov NTP server address
const char* ntpServerName = "time.nist.gov";
const int NTP_PACKET_SIZE = 48;         // NTP time stamp is in the first 48 bytes of the message
byte packetBuffer[ NTP_PACKET_SIZE];    // buffer to hold incoming and outgoing packets
WiFiUDP udp;                            // A UDP instance to let us send and receive packets over UDP

// NTP time set
//---------------------------------
unsigned long NTPtime;                // total number of seconds since 1900 for NTP

// RTC current time
//---------------------------------
tmElements_t tm;                      // current RTC time since 1970

// DS1307 module data for NVRAM & 24C32
//---------------------------------
#define ADDRESS_24C32      0x57
#define DS1307_ADDRESS     0x68
#define DS1307_NVRAM_BASE  0x08

byte Fday, Fmonth, Fyear, Fhour, Fminute, Fsecond;
unsigned long Ftime;                  // total number of seconds since 1970 for the end of operation

// other variables
//---------------------------------
#define ledPin  D4                    // wemos module internal led
int driftOffset = 30;                 // drift value under offset are negative
int drift = 0;                        // number of drift seconds per day
int defaultTZ = 1;                    // timezone
char consoleInput[] = "000";          // console input string
byte charCounter = 0;                 // console input index
bool reading = true;                  // console input flag

//
// SETUP
//_____________________________________________________________________________________________

void setup() {

  pinMode( ledPin, OUTPUT );
  delay(1000);
  
// set the Serial Monitor
//---------------------------------
  Serial.begin(250000);
  Serial.println("\nDS1307 drift detection and correction");
  Serial.println("-------------------------------------\n");

// look at the NVRAM cells
//---------------------------------
  Wire.begin();                       // initialise the I2C port
  RTC.read(tm);                       // just to check if the ship is correctly wired
  while( !RTC.chipPresent() ) {
    yield();
    static bool altern = false;
    altern = !altern;
    digitalWrite( ledPin, altern );    
    Serial.println("DS1307 ship no present, please insert one or check the wiring...");
    delay(1000);
    RTC.read(tm);                     // just to check if the ship is correctly wired
  } 
  byte Fcell0 = ReadNVRAM(0);
  byte Fcell1 = ReadNVRAM(1);
  Fday    = ReadNVRAM(4);
  Fmonth  = ReadNVRAM(3);
  Fyear   = ReadNVRAM(2);
  Fhour   = ReadNVRAM(5);
  Fminute = ReadNVRAM(6);
  Fsecond = ReadNVRAM(7);
  drift   = ReadNVRAM(8) - driftOffset;

// Serial Monitor initial message
//---------------------------------
  Serial.begin(250000);
  Serial.println("\nDS1307 drift detection and correction");
  Serial.println("-------------------------------------");
  Serial.println("\nThe drift is calculated in regard of NTP time, every time laps of 24 hours.");
  Serial.println("Then the drift value is saved in the DS1307 NAVRAM memory, cell 8,");
  Serial.println("under the formula: (20-'number-of-drift-seconds-per-day')");
  Serial.println("if there is a 24C32 EEPROM with the DS1307, the drift value is also saved in cell 0.");
  Serial.println("\nThe wemos blue led :");
  Serial.println("- stays light on during the full time of drift test");
  Serial.println("- switches off when the test is completed: 24 hours after the test started.");
  Serial.println("\nIt is not required to let the wemos in operation during the full period.");
  Serial.println("Once the period completed, the drift information are stored as soon as the Wemos will powered on back.\n\n");
  delay(1000);
  Serial.flush();

// get NTP and RTC time as close as possible
//---------------------------------
  while( getNTP() != 1 ) {
    yield();
    Serial.println("There is a problem to get NTP, check wifi!");
    delay(1000);
  }
  RTC.read(tm);                            // get RTC time in DS1307

// display NTP and RTC time
  Serial.print("\nNTP time get: ");
  DisplayTime();
  Serial.print("\nRTC time get: ");
  Serial.print(tm.Day); Serial.print("/");
  Serial.print(tm.Month); Serial.print("/");
  Serial.print(tm.Year + 1970); Serial.print(" ");
  Serial.print(tm.Hour); Serial.print(":");
  Print2digits(tm.Minute); Serial.print(":");
  Print2digits(tm.Second); Serial.println();

// case: operation in progress
//---------------------------------
  digitalWrite(ledPin, LOW);          // switch on the led
  if((Fcell0 == 0) || (Fcell1 == 0)) {
    setTime(Fhour, Fminute, Fsecond, Fday, Fmonth, Fyear);
    time_t t = now();
    Ftime = t;                        // get the remind number of second at end of operation
    Serial.print("This DS1307 drift calculation in progress until flagged time: ");
    DisplayTime();
  }
  else {
    RequestUpdateRTC();

// case: operation finished, cell0=1 & cell1=1
//---------------------------------
    if((Fcell0 == 1) && (Fcell1 == 1)) {
      digitalWrite(ledPin, HIGH);             // switch off the led
      Serial.print("This DS1307 is ready for use since the flag date/time : ");
      Serial.print(Fday); Serial.print("/");
      Print2digits(Fmonth); Serial.print("/");
      Serial.print(Fyear+2000);Serial.print(" ");
      Print2digits(Fhour); Serial.print(":");
      Print2digits(Fminute); Serial.print(":");
      Print2digits(Fsecond); Serial.println();
      Serial.print("\nDrift defined value is: ");
      Serial.println(drift);
  
      Serial.print("To redo the calcul of the drift during 24 hours, type: 'y' +'ENTER' to continue. ");
      charCounter = 0;
      reading = true;
      while( reading ) {
        if(Serial.available() != 0) {
          yield();
          char incomingChar = Serial.read();
          if( incomingChar != '\n' ) {
            consoleInput[charCounter] = incomingChar;
            charCounter++;
          }
          else {
            consoleInput[charCounter] ='\0';  // null character
            reading = false;
          }
        }
      }
      Serial.println(consoleInput);
      if(consoleInput[0] == 'y') {
        WriteNVRAM(0, 2);  // write something else than 0 or 1 in cell0
        Fcell0 = 2;
      }
      else {
        Serial.println("This DS1307 is ready to use with:");
        Serial.println("- drift value stored in NVRAM cell 8");
        Serial.println("- flagged NVRAM cells 0 and 1 set to 1.");
        delay(1000);
        Serial.println("\n\nBye bye");
        while( 1 ) { yield(); }
      }  
    }  // end of test Fcell0 == 1) || (Fcell1 == 1)

// case: operation never done
//---------------------------------
    if( Fcell0 != 1) {
      digitalWrite(ledPin, LOW);        // switch on the led
      Serial.print("This DS1307 need to be calibrated for at least 24 hours, type: 'y' +'ENTER' to continue. ");
      charCounter = 0;
      reading = true;
      while( reading ) {
        if(Serial.available() != 0) {
          yield();
          char incomingChar = Serial.read();
          if( incomingChar != '\n' ) {
            consoleInput[charCounter] = incomingChar;
            charCounter++;
          }
          else {
            consoleInput[charCounter] ='\0';  // null character
            reading = false;
          }
        }
      }
      Serial.println(consoleInput);
      if(consoleInput[0] == 'y') {
        RTC.set(NTPtime);               // set RTC with the actual time
        Ftime = NTPtime + 86400;        // add 24 hours to define the hour of end of operation
        setTime(Ftime);                 // record the flag date/time to the NVRAM
        WriteNVRAM(0, 0);
        WriteNVRAM(1, 0);
        WriteNVRAM(2, (year()-2000));
        WriteNVRAM(3, month());
        WriteNVRAM(4, day());
        WriteNVRAM(5, hour());
        WriteNVRAM(6, minute());
        WriteNVRAM(7, second());
        WriteNVRAM(8, driftOffset);
        Serial.print("This DS1307 drift calculation is starting now, and will continue until: ");
      }
      else {
        Serial.println("Action not confirmed.");
        delay(1000);
        Serial.println("\n\nBye bye");
        while( 1 ) { yield(); }
      }  
      DisplayTime();
    }  // end of test Fcell1 != 1
  }    // end of else Fcell0 == 0) || (Fcell1 == 0)

  setTime(NTPtime);               // format back to NTP time
  Serial.println("\nwait for status update at second 30");
  
}      // end of setup

//
// LOOP
//_____________________________________________________________________________________________

void loop() {

// operation in progress: 
//---------------------------------

// check if the DS1307 is still in place
  RTC.read(tm);                     // just to check if the ship is correctly wired
  if( !RTC.chipPresent()) {
    Serial.println("\nDS1307 ship has desapeared, the device is rebooting...");
    ESP.restart();
  }
  delay(1000);

// what is done every minute
  if(second() != 30) return;
  DisplayTime();

// compare RTC time with NTP, we suppose the total drift will not exceed 29 seconds
  RTC.read(tm);
  time_t t = now();
  byte RTCseconds = tm.Second;
  byte NTPseconds = second(t);
  drift = RTCseconds - NTPseconds;
  NTPtime = t;

  Serial.print("\t => RTC=");
  Serial.print(RTCseconds);
  Serial.print(" - NTP=");
  Serial.print(NTPseconds);
  Serial.print("\t => drift RTC-NTP = ");
  Serial.print(drift);
  
// verify the flag date/time reached state
  long remind = NTPtime - Ftime;
  if( remind < 0 ) {
    Serial.print("\t end test in: ");
    Serial.print(-remind / 3600); Serial.print("h");
    Print2digits(byte(((-remind) % 3600)/60)); Serial.println(" minutes");
  }
  else {
    float extraDays = remind / 86400;
    drift = float(drift/( 1 + extraDays ));
    WriteNVRAM(0, 1);                     // flag 'test done' in DS1307 NVRAM cell0
    WriteNVRAM(1, 1);                     // flag 'test done' in DS1307 NVRAM cell1
    WriteNVRAM(8, (drift+driftOffset));   // must be a positive number (type byte)
    //Write24C32(0, 1);                     // flag 'test done' in 24C32 cell0
    //Write24C32(1, 1);                     // flag 'test done' in 24C32 cell1
    //Write24C32(8, (drift+driftOffset));   // drift+driftOffset in 24C32 cell2
    time_t t = now();
    NTPtime = t;
    RTC.set(NTPtime);                     // update date/time in DS1307

    digitalWrite(ledPin, HIGH);           // switch off the led
    Serial.print("\n\nThis DS1307 is ready since: ");
    Fday    = ReadNVRAM(4);
    Fmonth  = ReadNVRAM(3);
    Fyear   = ReadNVRAM(2);
    Fhour   = ReadNVRAM(5);
    Fminute = ReadNVRAM(6);
    Fsecond = ReadNVRAM(7);
    Serial.print(Fday); Serial.print("/");
    Print2digits(Fmonth); Serial.print("/");
    Serial.print(Fyear+2000);Serial.print(" ");
    Print2digits(Fhour); Serial.print(":");
    Print2digits(Fminute); Serial.print(":");
    Print2digits(Fsecond); Serial.print(" so since: ");
    Serial.print(remind / 3600); Serial.print("hours ");
    Print2digits(byte((remind % 3600)/60)); Serial.println(" minutes");
    delay(1000);
    Serial.println("\n\nBye bye");
    while( 1 ) { yield(); }
  }

}     // end of loop

//============================================================================================
// list of functions
//============================================================================================

//
// RequestUpdateRTC() : request if RTC date/time must be updated
//____________________________________________________________________________________________

void RequestUpdateRTC() {
  Serial.print("\nDoes it needed to update the time? type: 'y' +'ENTER' to perform the time update. ");
  charCounter = 0;
  reading = true;
  while( reading ) {
    if(Serial.available() != 0) {
      yield();
      char incomingChar = Serial.read();
      if( incomingChar != '\n' ) {
        consoleInput[charCounter] = incomingChar;
        charCounter++;
      }
      else {
        consoleInput[charCounter] ='\0';  // null character
        reading = false;
      }
    }
  }
  Serial.println(consoleInput);
  if(consoleInput[0] == 'y') {
    time_t t = now();
    NTPtime = t;
    RTC.set(NTPtime);                     // update date/time in DS1307
    RTC.read(tm);
    Serial.print("\nNTP time get: ");
    DisplayTime();
    Serial.print("\nRTC time get: ");
    Serial.print(tm.Day); Serial.print("/");
    Serial.print(tm.Month); Serial.print("/");
    Serial.print(tm.Year + 1970); Serial.print(" ");
    Serial.print(tm.Hour); Serial.print(":");
    Print2digits(tm.Minute); Serial.print(":");
    Print2digits(tm.Second); Serial.println();
  }
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

//
// ReadNVRAM() : function to read DS1307 NVRAM 56 bytes memory
//____________________________________________________________________________________________

int ReadNVRAM(byte address) {

  Wire.beginTransmission(DS1307_ADDRESS);
  Wire.write(DS1307_NVRAM_BASE + address);
  Wire.endTransmission();
  Wire.requestFrom(DS1307_ADDRESS, (byte) 1);
  return Wire.read();
}

//
// WriteNVRAM() : function to write in DS1307 NVRAM 56 bytes memory
//____________________________________________________________________________________________

void WriteNVRAM(byte address, byte data) {

  Wire.beginTransmission(DS1307_ADDRESS);
  Wire.write(DS1307_NVRAM_BASE + address);
  Wire.write(data);
  Wire.endTransmission(); // Fin de transaction I2C
}

//
// Read24C32() : function to read 24C32 EEPROM
//____________________________________________________________________________________________

int  Read24C32( byte address ) {

  Wire.beginTransmission(ADDRESS_24C32);
  Wire.write((int)(address >> 8));    // MSB
  Wire.write((int)(address & 0xFF));  // LSB
  Wire.endTransmission();
  Wire.requestFrom(ADDRESS_24C32, (byte) 1);
  return Wire.read();
}

//
// Write24C32() : function to write in 24C32 EEPROM
//____________________________________________________________________________________________

void Write24C32( unsigned int address, byte data ) {
    Wire.beginTransmission(ADDRESS_24C32);
    Wire.write((int)(address >> 8));    // MSB
    Wire.write((int)(address & 0xFF));  // LSB
    Wire.write(data);
    Wire.endTransmission();
}

//
// ConnectWifi : se (re)connecte au wifi
//____________________________________________________________________________________________

byte ConnectWifi() {
  
  byte i = 0;             // petit compteur pour détecter la connexion au wifi
  byte imax = 10;         // nombre maximum de tentative de connexion au wifi

// AP will start if no wifi identifiers in memory or wrong identification
// AP can be accessed from ssid "AutoConnectAP" then IP address 192.168.4.1 within 150 seconds
// in cas of unsuccess after 150 seconds the wifi will not be defined
// for local intialization. Once its business is done, there is no need to keep it around
    WiFiManager monwifi;
//    monwifi.resetSettings();            // raz des identifiants mémorisés
//    monwifi.setTimeout(150);            // délai pour accéder au portail en secondes

// fetches ssid and pass from eeprom and tries to connect. If it does not connect it starts 
// an access point with the specified name and goes into a blocking loop awaiting configuration
    if(!monwifi.autoConnect("AutoConnectAP")) { Serial.println("non paramétré"); }
    else {
// Connect to Wi-Fi network with SSID and password
      Serial.print("connexion au Wifi en cours ");               
      while( (WiFi.status() != WL_CONNECTED) && (i++ < imax)) { 
        delay(500); 
        Serial.print(".");
      } 
    }   // fin de else sur monwifi.autoConnect

// si connexion au wifi
  if((i < imax) && (WiFi.localIP() != 0)) {
// envoie les infos de connexion
    Serial.println();
    Serial.println("Wifi connecté.");
    Serial.print("Address IP : ");
    Serial.println(WiFi.localIP());
    
    
// démarrage du service udp    
    Serial.println("Starting UDP");
    udp.begin(localPort);
    Serial.print("Local port: ");
    Serial.println(udp.localPort());
    return 1;
  
  }  // fin de test sur i
  else {
    Serial.println();
    Serial.println("pas de réseau wifi");
    return 0;
  }
}      // end of ConnectWifi()


//
// getNTP : fonction pour récupérer l'heure sur le serveur NTP
//____________________________________________________________________________________________

byte getNTP() {

  while( ConnectWifi() != 1 ) {
    yield();
    Serial.println("no wifi, cannot continue !");
    delay(1000);
  }

  int TZ = 0;             // timezone
  byte i = 0;             // petit compteur pour détecter la lecture du NTP
  byte imax = 40;         // nombre maximum de tentative de lecture du NTP
  
  WiFi.hostByName(ntpServerName, timeServerIP);  // get a random server from the pool
  
  do {
     Serial.print("sending NTP packet... ");
     Serial.println(i);
     memset(packetBuffer, 0, NTP_PACKET_SIZE);   // set all bytes in the buffer to 0
     // Initialize values needed to form NTP request
     packetBuffer[0] = 0b11100011;   // LI, Version, Mode
     packetBuffer[1] = 0;            // Stratum, or type of clock
     packetBuffer[2] = 6;            // Polling Interval
     packetBuffer[3] = 0xEC;         // Peer Clock Precision
     // 8 bytes of zero for Root Delay & Root Dispersion
     packetBuffer[12] = 49;
     packetBuffer[13] = 0x4E;
     packetBuffer[14] = 49;
     packetBuffer[15] = 52;
     // all NTP fields have been given values, now you can send a packet requesting a timestamp:
     udp.beginPacket(timeServerIP, 123);       // NTP requests are to port 123
     udp.write(packetBuffer, NTP_PACKET_SIZE);
     udp.endPacket();
     delay(1000);                              // wait to see if a reply is available
  } while(!udp.parsePacket() && (i++ < imax));   // tant qu'aucun packt n'est reçu

  if( i<imax ) {                     // We've received a packet, read the data from it
    udp.read(packetBuffer, NTP_PACKET_SIZE);   // read the packet into the buffer
 
    //the timestamp starts at byte 40 of the received packet and is four bytes,
    // or two words, long. First, esxtract the two words:
    unsigned long highWord = word(packetBuffer[40], packetBuffer[41]);
    unsigned long lowWord = word(packetBuffer[42], packetBuffer[43]);
    // combine the four bytes (two words) into a long integer
    // this is NTP time (seconds since Jan 1 1900):
    unsigned long secsSince1900 = highWord << 16 | lowWord;
    Serial.print("Seconds since Jan 1 1900 = ");
    Serial.println(secsSince1900);

    // now convert NTP time into everyday time:
    Serial.print("Unix time = ");
    // Unix time starts on Jan 1 1970. In seconds, that's 2208988800:
    const unsigned long seventyYears = 2208988800UL;
    // subtract seventy years:
    NTPtime = secsSince1900 - seventyYears;
    // print Unix time:
    Serial.println(NTPtime);
    setTime(NTPtime);                  // ajuste la date et l'heure

// search if winter or summer time
    int mois = month();
    int jour = day();
    int joursemaine = weekday();
    if((mois > 3 && mois < 10) || 
       (mois == 3  && (jour - joursemaine) > 22 ) || 
       (mois == 10 && (jour - joursemaine) < 23 )) TZ = defaultTZ +1;    
    else TZ = defaultTZ;               // winter time
    NTPtime = NTPtime + TZ*3600;
    setTime(NTPtime);
    return 1;
  }
  else return 0;
  
}   // fin de getNTP()
