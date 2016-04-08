/**
 * StreamHTTPClient.ino
 *
 *  Created on: 24.05.2015
 *
 */

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>

char headlines[20][128];
int line, loaded;
unsigned long lastMillis;
unsigned long lastUpdate=-600000;
char* monthTable PROGMEM = "Jan Feb Mar Apr May Jun Jul Aug Sep Oct Nov Dec";

void setup() {
  Serial.begin(115200);
  Serial1.begin(115200);
  Serial.println();
  Serial.println();
  Serial.println();
  for (uint8_t t = 4; t > 0; t--) {
    Serial.printf("[SETUP] WAIT %d...\n", t);
    Serial.flush();
    delay(1000);
  }
  WiFi.begin("esports-league", "cuesports");
}

void loop() {
  unsigned long now=millis();
  if(now-lastUpdate > 600000 && WiFi.status() == WL_CONNECTED) {
    line = 0;
//    loaded = loadFeed("http://feeds.reuters.com/reuters/technologyNews?format=xml",2, 10);
    loaded = loadFeed("http://www.newslookup.com/rss/wires/newslookup-wires.rss", 1, 10);
    loaded += loadFeed("http://rss.accuweather.com/rss/liveweather_rss.asp?locCode=10003", 1, 2);
    Serial.print(loaded);
    Serial.println(" headlines loaded");
    getTime();
    lastUpdate = now;
  } else {
    static bool printed;
    if(!printed) {
      for (int i = 0; i < loaded; i++) {
        Serial.print(i);
        Serial.print(' ');
        Serial.println(headlines[i]);
      }
    printed = true;
    }
    static int i;
    if(now-lastMillis > 10000) {
      Serial.println(headlines[i]);
//      Serial1.write(1);
//      Serial1.print(16);
//      Serial1.print(14);
      Serial1.println(headlines[i++]);
      lastMillis=now;
      if(i>=loaded) i=0;
    }
  }
}

void fixCharRef(char* reference, char sub) {
  char* schar;
  int refLen = strlen(reference);
  for(int i=0; i<10; i++) {
    while (schar = strstr(headlines[i], reference)) {
      *schar = sub;
      for (schar += refLen; *schar != 0; schar++) {
        *(schar-refLen+1) = *schar;
      }
      *(schar-refLen+1) = 0;
    }
  }
}

int loadFeed(char* url, int skip, int maxLines) {
  int src = 0; //source index (from buff)
  int dest = 0; //destination index
  bool copying = false;
  int count = 0;
  
  HTTPClient http;
  http.begin(url);
  Serial.print("Load ");
  Serial.println(url);
  int httpCode = http.GET();
  if (httpCode == HTTP_CODE_OK) {
    int len = http.getSize();
    uint8_t buff[128] = { 0 }; //buffer
    WiFiClient * stream = http.getStreamPtr();
    while (http.connected() && (len > 0 || len == -1)) {
      size_t size = stream->available(); //total size
      if (size) {
        int c = stream->readBytes(buff, ((size > sizeof(buff)) ? sizeof(buff) : size));
        if (len > 0) {
          len -= c;
        }
        if (!copying) {
          char* find = strstr((char*) buff, "<ti");
          int ind = find - (char*)buff;
          if (find && skip-- <= 0) {
            copying = true;
            if(ind < c - 7) {
              src = ind + 7;
            } else { //at eol
              Serial.println("eol");
              src=7-(c-ind);
              continue;
            }
          }
        }
        if (copying) {
          for (int i = src; i < c; i++) {
            if (buff[i] != '<') {
              headlines[line][dest++] = buff[i]; //copy char
              if (dest > 255) {
                Serial.println("overflow");
                headlines[line][255] = 0;
                copying = false;
                line++;
                dest = 0;
                count++;
                break;
              }
            } else {
              headlines[line][dest] = 0;
              copying = false;
              line++;
              dest = 0;
              count++;
              break;
            }
          }
        }
        if (line >= sizeof(headlines) || count >= maxLines) {
          break;
        }
      }
      delay(1);
      src = 0;
    }
    Serial.println("HTTP close");
  
  } else {
    Serial.printf("HTTP ERROR %s\n", http.errorToString(httpCode).c_str());
  }
  http.end();
  fixCharRef("&apos;", '\'');
  fixCharRef("&amp;", '&');
  fixCharRef("&quot;", '"');
  return count;
}

void getTime() {
  WiFiClient client;
  while (!!!client.connect("google.com", 80)) {
    Serial.println("connection failed, retrying...");
  }
  client.print("HEAD / HTTP/1.1\r\n\r\n");
  while(!!!client.available()) {
     yield();
  }
  while(client.available()){ 
    if (client.read() == 't' && client.read() == 'e' && client.read() == ':') {
      client.read();
      char dateStr[30];
      dateStr[29] = 0;
      client.readBytesUntil('G', dateStr, 30);
      char* c = dateStr;
      while(c=strchr(c+1, ' ')) *c=0;
      c = dateStr+17;
      while(c=strchr(c+1, ':')) *c=0;
      Serial1.write('\005');
      Serial1.write(dateStr+5, 3); //day
      Serial1.printf("%02d", (strstr(monthTable, dateStr+8)-monthTable)/4+1); //month
      Serial1.write(dateStr+11, 15); //year, hr, min, sec
      client.stop();
    }
  }
}

