// MAX7219 Scrolling Display with DS1307
// Adapted from the Parola examples
//

#include <MD_Parola.h>
#include <MD_MAX72xx.h>
#include <Time.h>  
#include <Wire.h>  
#include <DS1307RTC.h>
#include <SPI.h>

uint8_t degC[] = { 6, 3, 3, 56, 68, 68, 68 };
uint8_t degF[] = { 6, 3, 3, 124, 20, 20, 4 };
uint8_t heart[] = {5, 28, 62, 124, 62, 28};
uint8_t mode = 0;
uint8_t msgIndex = 0;
uint8_t msgCount = 1;
char tm[3] = "AM";
char monthBuff[10];
unsigned long timer = 1357041600;
#define DEFAULT_TIME 1357041600 // Jan 1 2013 

// Parola definitions
#define	MAX_DEVICES	12
#define	CS_PIN		10
MD_Parola P = MD_Parola(CS_PIN, MAX_DEVICES); //using SPI

#define INITIAL_SPEED 0
#define MSG_PAUSE 1250
#define CLK_PAUSE 1000
#define CLK_INTERVAL 5000

#define SET_RTC_CHAR '\005'
#define SET_EFFECT_CHAR '\001'
#define LINE_SEPARATOR '\r'
#define MSG_SEPARATOR '\n'

textEffect_t  enterEffect = SCROLL_DOWN;
textEffect_t  exitEffect = SCROLL_DOWN;
textEffect_t	effectList[] =
{
	PRINT,
	SCAN_HORIZ,
	SCROLL_LEFT,
	WIPE,
	SCROLL_UP_LEFT,
	SCROLL_UP,
	OPENING_CURSOR,
	GROW_UP,
	SCROLL_UP_RIGHT,
	BLINDS,
	CLOSING,
	GROW_DOWN,
	SCAN_VERT,
	SCROLL_DOWN_LEFT,
	WIPE_CURSOR,
	DISSOLVE,
	OPENING,
	CLOSING_CURSOR,
	SCROLL_DOWN_RIGHT,
	SCROLL_RIGHT,
	SCROLL_DOWN,
};

//Message buffers
#define	LINE_MAX 128
#define	MAX_LINES 3
char curMessage[LINE_MAX]; //display buffer
char newMessage[MAX_LINES][LINE_MAX]; //message buffer

bool newMessageAvailable = false;

void readSerial(void)
{
  uint8_t arrIndex = 0;
  char nextChar = Serial.read();
  if(nextChar == SET_RTC_CHAR)
  {
    int dates[6];
    char buf[5];
    buf[4]=0;
    for(uint8_t i=0; i<6; i++) {
      uint8_t bytes = Serial.readBytesUntil(0,buf,4);
      if(bytes) {
        buf[bytes] = 0;
        dates[i] = atoi(buf);
      } else {
        i--;
      }
    }
    setTime(dates[3],dates[4],dates[5],dates[0],dates[1],dates[2]);
    RTC.set(now());
    RTC.set(RTC.get()-14400); //GMT->EST
    setTime(RTC.get());
    Serial.println(F("Clock Set"));
  }
  else
  {
    if(nextChar == SET_EFFECT_CHAR) {
      enterEffect = effectList[Serial.parseInt()];
      exitEffect = effectList[Serial.parseInt()];
    } else {
      enterEffect = exitEffect = SCROLL_DOWN;
    }
    while(nextChar >= 0 && (nextChar != '\n')) {
      Serial.println("read line");
      newMessage[arrIndex][0] = nextChar;
      unsigned int bytes = Serial.readBytesUntil('\r', newMessage[arrIndex]+1, sizeof(newMessage[0])-1);
      newMessage[arrIndex++][bytes+1] = 0;
      nextChar = Serial.read();
      if(arrIndex >= MAX_LINES) {
        break;
      }
    }
    msgCount = arrIndex;
    newMessageAvailable = true;
    Serial.println(newMessage[0]);
  }
  while(Serial.available()) Serial.read(); //flush buffer
}

void setup()
{
  Serial.begin(115200);
  Serial.println("[Initializing]");
  P.begin();
  P.displayClear();
  P.displaySuspend(false);
//  P.addChar('\007', degF);
  P.displayText(curMessage, CENTER, INITIAL_SPEED, CLK_PAUSE, enterEffect, exitEffect);
  
  setSyncProvider(RTC.get);
  if(timeStatus()!= timeSet) 
     Serial.println(F("RTC ERR"));
  else
     Serial.println(F("RTC OK"));  
  SPI.setClockDivider(SPI_CLOCK_DIV2);
}

void loop() 
{
  if(Serial.available())
  {
    readSerial();
  }
  if (P.displayAnimate()) 
  {
      if(millis() - timer > CLK_INTERVAL && msgIndex ==0)
      {
        switch(mode)
        {
          case 0:
            P.setPause(CLK_PAUSE);
            P.setSpeed(0);
            P.setTextEffect(SCROLL_DOWN, SCROLL_DOWN);
            sprintf(curMessage, "%s %i, %i", monthStr(month()), day(), year());
            break;
          case 1:
          {
            P.setIntensity(map(analogRead(A7),10,400,0,15)); //ambient brightness
            sprintf(curMessage, "%i:%02i %s", get12hr(), minute(), tm);
            break;
          }
          default:
            P.setPause(MSG_PAUSE);
            if(msgCount == 1) {
              P.setPause(CLK_INTERVAL);
            }
            timer = millis();
            strcpy(curMessage, newMessage[msgIndex]);
            if(strlen(curMessage) > 16)
            {
              P.setTextEffect(SCROLL_LEFT, SCROLL_LEFT);
              P.setSpeed(20);
              P.setPause(0);
            }
            ++msgIndex%=msgCount;
            newMessageAvailable = false;
        }
      ++mode%=3;
      } else if(msgCount>1)
      {
        P.setSpeed(0);
        P.setTextEffect(SCROLL_DOWN, SCROLL_DOWN);
        P.setPause(MSG_PAUSE);
        strcpy(curMessage, newMessage[msgIndex]);
        if(strlen(curMessage) > 16)
        {
          P.setTextEffect(SCROLL_LEFT, SCROLL_LEFT);
          P.setSpeed(20);
          P.setPause(0);
        }
        ++msgIndex%=msgCount;
        newMessageAvailable = false;
      }
      P.displayReset();
  }
}

size_t mon2str(uint8_t mon, char *psz)
{
  static const __FlashStringHelper* months[] = 
  { 
    F("January"),
    F("February"),
    F("March"),
    F("April"),
    F("May"),
    F("June"),
    F("July"),
    F("August"),
    F("September"),
    F("October"),
    F("November"),
    F("December")
  };
  strncpy_P(psz, (const char PROGMEM *) months[mon-1], 10);
  return strlen_P((const char PROGMEM *) months[mon-1]);
}

int get12hr()
{
  int hr = hour();
  strcpy(tm,"AM");
  if(hr == 12) {
    strcpy(tm,"PM");
  } else if(hr > 12) {
    strcpy(tm,"PM");
    hr -= 12;
  } else if(hr == 0)
    hr = 12; 
  return hr;
}


