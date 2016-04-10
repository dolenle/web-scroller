/**
 * Parola-MAX7219 Serial Message Display with Clock
 * Dolen Le
 */
 
#include <MD_Parola.h>
#include <MD_MAX72xx.h>
#include <Time.h>  
#include <Wire.h>  
#include <DS1307RTC.h>
#include <SPI.h>

// Parola definitions
#define  MAX_DEVICES 12
#define CS_PIN    10

#define INITIAL_SPEED 0
#define MSG_PAUSE 1250
#define CLK_PAUSE 1000
#define CLK_INTERVAL 5000

#define SET_RTC_CHAR '\005'
#define SET_EFFECT_CHAR '\001'
#define LINE_SEPARATOR '\r'
#define MSG_SEPARATOR '\n'

enum modes {DISP_DATE, DISP_TIME, DISP_MSG};

textEffect_t  enterEffect = SCROLL_DOWN;
textEffect_t  exitEffect = SCROLL_DOWN;
uint8_t effectSpeed = 2;

MD_Parola P = MD_Parola(CS_PIN, MAX_DEVICES); //using SPI
uint8_t degC[] = { 6, 3, 3, 56, 68, 68, 68 };
uint8_t degF[] = { 6, 3, 3, 124, 20, 20, 4 };
uint8_t colon[] = {2, 102, 102};
char ap[3] = "AM";
unsigned long timer = 1357041600;

//Message buffers
#define	MAX_LENGTH 192
#define	MAX_LINES 3
#define MAX_MSG MAX_LENGTH*MAX_LINES
#define DISPLAY_WIDTH 16 //characters which can fit
char curMessage[MAX_LENGTH]; //display buffer
char newMessage[MAX_MSG]; //message buffer
char* nextLine = newMessage;
uint8_t mode = 0;
uint8_t lineInd = 0;
uint8_t lineCount = 1;

void readSerial(void) {
  char nextChar = Serial.read();
  if(nextChar == SET_RTC_CHAR) { //Set time
    int dates[6];
    char buf[5];
    buf[4]=0;
    for(uint8_t i = 0; i < 6; i++) { //parse time into array
      uint8_t bytes = Serial.readBytesUntil(0,buf,4);
      if(bytes) {
        buf[bytes] = 0;
        dates[i] = atoi(buf);
      } else {
        i--;
      }
    }
    tmElements_t data = {dates[5],dates[4],dates[3],0,dates[0],dates[1],dates[2]-1970};
    setTime(makeTime(data)-14400); //GMT->EST
    Serial.println(F("Clock Set"));
  } else if(nextChar == SET_EFFECT_CHAR) {
    enterEffect = (textEffect_t) Serial.parseInt();
    exitEffect = (textEffect_t) Serial.parseInt();
    effectSpeed = (uint8_t) Serial.parseInt();
    Serial.println("Effect set");
  } else if(nextChar >= 0) { //incoming message
    lineCount = 0;
    char* msgPtr = newMessage;
    while(nextChar != MSG_SEPARATOR) {
      unsigned int space = newMessage+MAX_MSG-msgPtr;
      *msgPtr++ = nextChar;
      unsigned int bytes = Serial.readBytesUntil(LINE_SEPARATOR, msgPtr, (space>MAX_LENGTH-1 ? MAX_LENGTH-1 : space));
      *(msgPtr+=bytes) = 0; //terminator
      lineCount++;
      msgPtr++;
      if(!Serial.readBytes(&nextChar, 1) || msgPtr > newMessage+MAX_MSG) {
        break;
      }
    }
    Serial.print(lineCount);
    Serial.print(" lines - ");
    Serial.println(newMessage);
  }
  while(Serial.available()) Serial.read(); //flush buffer
}

void setup() {
  Serial.begin(115200);
  Serial.setTimeout(100);
  Serial.println(F("Initializing"));
  
  P.begin();
  P.addChar(':', colon);
  P.displayClear();
  P.displaySuspend(false);
  P.setIntensity(0);
  P.displayText(curMessage, CENTER, INITIAL_SPEED, CLK_PAUSE, enterEffect, exitEffect);
  
  setSyncProvider(RTC.get);
  if(timeStatus() != timeSet) {
     strcpy(newMessage, "RTC ERR");
  } else {
     strcpy(newMessage, "RTC OK");
  }
  SPI.setClockDivider(SPI_CLOCK_DIV2); //8MHz SPI
}

void loop() {
  if(Serial.available()) {
    readSerial();
  }
  if (P.displayAnimate())  {
    if(millis() - timer > CLK_INTERVAL && lineInd == 0) {
      ++mode %= DISP_MSG+1;
    }
    switch(mode) {
      case DISP_DATE:
        P.setPause(CLK_PAUSE);
        P.setSpeed(effectSpeed);
        P.setTextEffect(enterEffect, exitEffect);
        sprintf(curMessage, "%s %i, %i", monthStr(month()), day(), year());
        break;
      case DISP_TIME: {
        P.setIntensity(map(analogRead(A7),10,400,0,15)); //ambient brightness
        sprintf(curMessage, "%i:%02i %s", get12hr(), minute(), ap);
        break;
      }
      default:
        strncpy(curMessage, nextLine, MAX_LENGTH);
        if(strlen(curMessage) > DISPLAY_WIDTH) {
          P.setTextEffect(SCROLL_LEFT, SCROLL_LEFT);
          P.setSpeed(20);
          P.setPause(0);
        } else {
          P.setTextEffect(enterEffect, exitEffect);
          P.setSpeed(effectSpeed);
          if(lineCount == 1) {
            P.setPause(CLK_INTERVAL);
          } else {
            P.setPause(MSG_PAUSE);
          }
        }
        if(++lineInd >= lineCount) {
          lineInd = 0;
          nextLine = newMessage;
        } else {
          nextLine += strlen(nextLine)+1;
        }
    }
    P.displayReset();
  }
}

int get12hr() {
  int hr = hour();
  ap[0] = 'P';
  if(hr < 12) {
    if(hr == 0) {
      hr = 12;
    }
    ap[0] = 'A';
  } else if(hr > 12) {
    hr -= 12;
  }
  return hr;
}


