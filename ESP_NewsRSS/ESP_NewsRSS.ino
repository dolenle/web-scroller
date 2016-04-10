/**
 * ESP8266 RSS Feed
 * Dolen Le
 */

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266WebServer.h>

#define MAX_FEEDS 5
#define MAX_LINES 25
#define MAX_LENGTH 192
#define URL_LENGTH 96

enum textEffect_t
{
  NO_EFFECT,    ///< Used as a place filler, executes no operation
  PRINT,        ///< Text just appears (printed)
  SCROLL_UP,    ///< Text scrolls up through the display
  SCROLL_DOWN,  ///< Text scrolls down through the display
  SCROLL_LEFT,  ///< Text scrolls right to left on the display
  SCROLL_RIGHT, ///< Text scrolls left to right on the display
  SLICE,        ///< Text enters and exits a slice (column) at a time from the right
  MESH,         ///< Text enters and exits in columns moving in alternate direction (U/D)
  FADE,         ///< Text enters and exits by fading from/to 0 and intensity setting
  DISSOLVE,     ///< Text dissolves from one display to another
  BLINDS,       ///< Text is replaced behind vertical blinds
  WIPE,         ///< Text appears/disappears one column at a time, looks like it is wiped on and off
  WIPE_CURSOR,  ///< WIPE with a light bar ahead of the change
  SCAN_HORIZ,   ///< Scan one column at a time then appears/disappear at end
  SCAN_VERT,    ///< Scan one row at a time then appears/disappear at end
  OPENING,      ///< Appear and disappear from the center of the display, towards the ends
  OPENING_CURSOR, ///< OPENING with light bars ahead of the change
  CLOSING_NOCURSOR,///< Appear and disappear from the ends of the display, towards the middle
  CLOSING_CURSOR, ///< CLOSING with light bars ahead of the change
  SCROLL_UP_LEFT,   ///< Text moves in/out in a diagonal path up and left (North East)
  SCROLL_UP_RIGHT,  ///< Text moves in/out in a diagonal path up and right (North West)
  SCROLL_DOWN_LEFT, ///< Text moves in/out in a diagonal path down and left (South East)
  SCROLL_DOWN_RIGHT,///< Text moves in/out in a diagonal path down and right (North West)
  GROW_UP,      ///< Text grows from the bottom up and shrinks from the top down 
  GROW_DOWN,    ///< Text grows from the top down and and shrinks from the bottom up
};

char headlines[MAX_LINES][MAX_LENGTH];
int line;
unsigned long rss_interval = 600000;
unsigned long clock_interval = 3600000;
unsigned long disp_interval = 12000;
unsigned long lastDisp;
unsigned long lastUpdate = -rss_interval;
unsigned long lastTimeSet = -clock_interval;
char* monthTable PROGMEM = "Jan Feb Mar Apr May Jun Jul Aug Sep Oct Nov Dec";
char* redirHeader PROGMEM = "HTTP/1.1 301 OK\r\nLocation: /\r\nCache-Control: no-cache\r\n\r\n";
char* effectSelect PROGMEM = "<option value=3>-</option><option value=0>NO_EFFECT</option><option value=1>PRINT</option><option value=2>SCROLL_UP</option><option value=3>SCROLL_DOWN</option><option value=4>SCROLL_LEFT</option><option value=5>SCROLL_RIGHT</option><option value=6>SLICE</option><option value=7>MESH</option><option value=8>FADE</option><option value=9>DISSOLVE</option><option value=10>BLINDS</option><option value=11>WIPE</option><option value=12>WIPE_CURSOR</option><option value=13>SCAN_HORIZ</option><option value=14>SCAN_VERT</option><option value=15>OPENING</option><option value=16>OPENING_CURSOR</option><option value=17>CLOSING</option><option value=18>CLOSING_CURSOR</option><option value=19>SCROLL_UP_LEFT</option><option value=20>SCROLL_UP_RIGHT</option><option value=21>SCROLL_DOWN_LEFT</option><option value=22>SCROLL_DOWN_RIGHT</option><option value=23>GROW_UP</option><option value=24>GROW_DOWN</option></select>";

uint8_t numFeeds = 2;
uint8_t feedSkip[MAX_FEEDS] = {1, 2};
uint8_t feedLines[MAX_FEEDS] = {10, 1};
char feedURLs[MAX_FEEDS][URL_LENGTH] = {
  "http://www.newslookup.com/rss/wires/newslookup-wires.rss",
  "http://rss.accuweather.com/rss/liveweather_rss.asp?locCode=10003"
};

//WiFi info
char ap_ssid[32] = "esp8266";
char st_ssid[32] = "esports-league";
char st_pass[32] = "cuesports";

ESP8266WebServer server(80); //http server on port 80

void setup() {
  Serial.begin(115200);
  Serial1.begin(115200);
  
  WiFi.begin(st_ssid, st_pass);
  WiFi.softAP(ap_ssid);
  
  server.on("/", pageHandler);
  server.on("/msg", msgHandler);
  server.on("/feed", feedHandler);
  server.on("/clock", updateTime);
  server.on("/reset", resetHandler);
  server.on("/wifi", wifiHandler);
  server.on("/get", loadMsg);
  server.on("/int", timingHandler);
  server.on("/effect", effectHandler);
  server.on("/ota", HTTP_POST, [](){
      server.sendContent(redirHeader);
      ESP.restart();
    }, otaHandler);
  server.begin();
  
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    server.handleClient();
    Serial.print(".");
  }
  Serial.println(WiFi.localIP());
  Serial.println(WiFi.softAPIP());
}

void loop() {
  server.handleClient();
  unsigned long now=millis();
  if(now-lastUpdate > rss_interval) {
    updateFeeds();
    loadMsg();
    lastUpdate = now;
  } else if(now-lastTimeSet > clock_interval) {
    updateTime();
    lastTimeSet = now;
    Serial.println("TimeSet");
  } else {
    static int i;
    if(now-lastDisp > disp_interval) {
      Serial.println(headlines[i]);
      Serial1.println(headlines[i++]);
      lastDisp=now;
      if(i>=line) i=0;
    }
  }
}

//replace reference string with character
void fixCharRef(char* reference, char sub) {
  char* schar;
  int refLen = strlen(reference);
  for(int i = 0; i < line; i++) {
    while (schar = strstr(headlines[i], reference)) {
      *schar = sub;
      for(schar += refLen; *schar != 0; schar++) {
        *(schar-refLen+1) = *schar;
      }
      *(schar-refLen+1) = 0;
    }
  }
}

//load up to maxLines titles from the feed, skipping at beginning
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
  if(httpCode == HTTP_CODE_OK) {
    int len = http.getSize();
    uint8_t buff[128] = { 0 }; //buffer
    WiFiClient * stream = http.getStreamPtr();
    while (http.connected() && (len > 0 || len == -1)) {
      size_t size = stream->available(); //total size
      if(size) {
        int c = stream->readBytes(buff, ((size > sizeof(buff)) ? sizeof(buff) : size));
        if(len > 0) {
          len -= c;
        }
        if(!copying) {
          char* find = strstr((char*) buff, "<ti");
          int ind = find - (char*)buff;
          if(find && skip-- <= 0) {
            copying = true;
            if(ind < c - 7) {
              src = ind + 7;
            } else { //at eol
              src=7-(c-ind);
              continue;
            }
          }
        }
        if(copying) {
          for(int i = src; i < c; i++) {
            if(buff[i] != '<') {
              headlines[line][dest++] = buff[i]; //copy char
              if(buff[i] == '\n') dest--; //skip newlines
              if(dest > 255) {
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
        if(line >= MAX_LINES || count >= maxLines) {
          break;
        }
      }
      delay(1);
      src = 0;
    }
    Serial.println("HTTP close");
  
  } else {
    Serial.printf("HTTP error %d\n", httpCode);
  }
  http.end();
  fixCharRef("&apos;", '\'');
  fixCharRef("&amp;", '&');
  fixCharRef("&quot;", '"');
  fixCharRef("\xe2\x80\x99", '\'');
  fixCharRef("\r\n", '\r');
  return count;
}

void updateTime() {
  WiFiClient client;
  while(!client.connect("google.com", 80)) {
    Serial.println("Time connect error");
  }
  client.print("HEAD / HTTP/1.1\r\n\r\n");
  while(!client.available()) {
     yield();
  }
  while(client.available()) { 
    if(client.read() == 't' && client.read() == 'e' && client.read() == ':') { //Date:
      client.read();
      char dateStr[30];
      dateStr[29] = 0;
      client.readBytesUntil('G', dateStr, 29);
      char* c = dateStr;
      while(c=strchr(c+1, ' ')) *c=0; //tokenize space
      c = dateStr+17;
      while(c=strchr(c+1, ':')) *c=0; //tokenize colon
      Serial1.write('\005');
      Serial1.write(dateStr+5, 3); //day
      Serial1.printf("%02d", (strstr(monthTable, dateStr+8)-monthTable)/4+1); //month
      Serial1.write(dateStr+11, 15); //year, hr, min, sec
      client.stop();
    }
  }
  server.sendContent(redirHeader);
}

void loadMsg() {
  Serial.println("loadMsg");
  HTTPClient http;
  http.begin("http:\/\/dolenle.com/led/getMsg.php?key=URl33tHAXORlol4lyef");
  int httpCode = http.GET();
  if(httpCode == HTTP_CODE_OK && line < MAX_LINES-1) {
    String payload = http.getString();
    Serial.println(payload);
    payload.toCharArray(headlines[line++], sizeof(headlines[0]));
  } else {
    Serial.println("Error");
  }
  http.end();
  fixCharRef("\r\n", '\r');
  server.sendContent(redirHeader);
}

int updateFeeds() {
  line = 0;
  for(int i = 0; i < numFeeds && line < MAX_LINES-1; i++) {
    loadFeed(feedURLs[i], feedSkip[i], feedLines[i]);
  }
  Serial.print(line);
  Serial.println(" titles loaded:");
  for(int i = 0; i < line; i++) {
    Serial.print(i);
    Serial.print(' ');
    Serial.println(headlines[i]);
  }
  Serial.println();
  return line;
}

//Main server page
void pageHandler() {
  String content = "<html><head><title>web-scroller</title><style>body{font-family:Helvetica,Arial,Sans-Serif;}</style></head><body><h1>ESP8266 web-scroller</h1><h4><a href='/clock'>Update Clock</a> | <a href='/reset'>Reset Display</a> | <a href='/get'>Get Message</a></h4><form action='/msg' method='POST'>Add Message:<input name='msg' size=90> <input type='submit'></form>Animation:<form action='/effect'>Enter: <select name='enter'>";
  content += effectSelect;
  content += " Exit: <select name='exit'>";
  content += effectSelect;
  content += "<input type=submit></form>Loaded:<ol>";
  int i;
  for(int i = 0; i < line; i++) {
    content += "<li>";
    content += headlines[i];
    content += "</li>";
  }
  content += "</ol>Feeds:<form action='/feed' method='POST'>";
  for(i = 0; (numFeeds < MAX_FEEDS && i <= numFeeds) || (numFeeds == MAX_FEEDS && i < numFeeds); i++) {
    content += "URL:<input name='url"+String(i)+"' size=80 value='";
    content += feedURLs[i];
    content += "'> Skip:<input name='skip"+String(i)+"' size=3 value='"+String(feedSkip[i])+"'> Max:<input name='max"+String(i)+"' size=3 value='"+String(feedLines[i])+"'><br>";
  }
  content += "<input type='submit'></form>WiFi Settings:<form action='/wifi' method='POST'>SSID:<input name='st_ssid' value='"+String(st_ssid)+"'> PASS:<input type='password' name='st_pass' placeholder='*****'> AP SSID:<input name='ap_ssid' value='"+String(ap_ssid)+"'> <input type='submit'></form>Intervals:<form action='/int'>Feed:<input name='rss' size=9 value="+String(rss_interval)+"> Clock:<input name='time' size=9 value="+String(clock_interval)+"> Display:<input name='disp' size=9 value="+String(disp_interval)+"><input type='submit'></form><form method='POST' action='/ota' enctype='multipart/form-data'>Firmware: <input type='file' name='update'><input type='submit'></form></body></html>";
  server.send(200, "text/html", content);
}

//Manually add a message
void msgHandler() {
  if(server.hasArg("msg")) {
    if(line < MAX_LINES-1) {
      server.arg("msg").toCharArray(headlines[line++], sizeof(headlines[0]));
    }
    Serial1.println(server.arg("msg"));
  }
  server.sendContent(redirHeader);
}

//Add/remove feed URLs
void feedHandler() {
  char urlID[5]="url", skipID[6]="skip", maxID[5]="max";
  uint8_t i;
  for(i = 0; i < MAX_FEEDS; i++) {
    strcpy(urlID+3, String(i).c_str());
    strcpy(skipID+4, String(i).c_str());
    strcpy(maxID+3, String(i).c_str());
    if(server.arg(urlID).length() > 1) {
      server.arg(urlID).toCharArray(feedURLs[i], sizeof(feedURLs[0]));
      feedSkip[i] = server.arg(skipID).toInt();
      feedLines[i] = server.arg(maxID).toInt();
    } else {
      feedSkip[i] = feedLines[i] = feedURLs[i][0] = 0;
      break;
    }
  }
  numFeeds = i;
  updateFeeds();
  server.sendContent(redirHeader);
}

//Reset the display driver
void resetHandler() {
  pinMode(0, OUTPUT);
  delay(10);
  pinMode(0, INPUT);
  server.sendContent(redirHeader);
}

//Update WiFi Settings
void wifiHandler() {
  if(server.hasArg("st_ssid") && server.hasArg("st_pass") && server.hasArg("ap_ssid")) {
    server.arg("st_ssid").toCharArray(st_ssid, sizeof(st_ssid));
    if(server.arg("st_pass").length()) {
      server.arg("st_pass").toCharArray(st_pass, sizeof(st_pass));
    }
    server.arg("ap_ssid").toCharArray(ap_ssid, sizeof(ap_ssid));
    WiFi.disconnect();
    WiFi.begin(st_ssid, st_pass);
    WiFi.softAP(ap_ssid);
    while (WiFi.status() != WL_CONNECTED) {
      delay(500);
      Serial.print(".");
    }
    Serial.println("WifiSet");
  } else {
    Serial.println("WifiErr");
  }
}

//Update Timing Settings
void timingHandler() {
  if(server.hasArg("rss") && server.hasArg("time") && server.hasArg("disp")) {
    rss_interval = server.arg("rss").toInt();
    clock_interval = server.arg("time").toInt();
    disp_interval = server.arg("disp").toInt();
  }
  server.sendContent(redirHeader);
}

void effectHandler() {
  if(server.hasArg("enter") && server.hasArg("exit")) {
    Serial1.write(1);
    Serial1.print(server.arg("enter"));
    Serial1.write(' ');
    Serial1.print(server.arg("exit"));
    Serial1.write(' ');
  }
  server.sendContent(redirHeader);
}

void otaHandler() {
  HTTPUpload& upload = server.upload();
  if(upload.status == UPLOAD_FILE_START){
    Serial.setDebugOutput(true);
    Serial.printf("Update: %s\n", upload.filename.c_str());
    uint32_t maxSketchSpace = (ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000;
    if(!Update.begin(maxSketchSpace)){//start with max available size
      Update.printError(Serial);
    }
  } else if(upload.status == UPLOAD_FILE_WRITE){
    if(Update.write(upload.buf, upload.currentSize) != upload.currentSize){
      Update.printError(Serial);
    }
  } else if(upload.status == UPLOAD_FILE_END){
    if(Update.end(true)){ //true to set the size to the current progress
      Serial.printf("Update Success: %u\nRebooting...\n", upload.totalSize);
    } else {
      Update.printError(Serial);
    }
    Serial.setDebugOutput(false);
  }
  yield();
}

