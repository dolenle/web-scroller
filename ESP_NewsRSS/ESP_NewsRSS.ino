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
uint8_t feedLines[MAX_FEEDS] = {5, 1};
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

  resetDisplay();
  
  WiFi.begin(st_ssid, st_pass);
  WiFi.softAP(ap_ssid);
  
  server.on("/", pageHandler);
  server.on("/msg", msgHandler);
  server.on("/feed", feedHandler);
  server.on("/wifi", wifiHandler);
  server.on("/int", timingHandler);
  server.on("/effect", effectHandler);
  server.on("/ota", HTTP_POST, [](){
      server.send(200, "text/plain", (Update.hasError())?"FAIL":"OK");
      ESP.restart();
    }, otaHandler);
  server.begin();
  
  while (WiFi.status() != WL_CONNECTED) {
    server.handleClient();
    delay(500);
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
    loadMTAServiceStatus();
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

//load up to limit titles from the feed, skipping at beginning
int loadFeed(char* url, int skip, int limit) {
  int count = 0;
  HTTPClient http;
  http.begin(url);
  Serial.print("Load ");
  Serial.println(url);
  int httpCode = http.GET();
  if(httpCode == HTTP_CODE_OK) {
    WiFiClient * stream = http.getStreamPtr();
    while (stream->available() && stream->find("<title") && line < MAX_LINES && count < limit) {
      if(stream->find('>') && skip-- <= 0) {
        String title = stream->readStringUntil('<');
        title.toCharArray(headlines[line++], sizeof(headlines[0]));
        count++;
      }
    }
    Serial.println("Done");
  } else {
    Serial.printf("HTTP error %d\n", httpCode);
  }
  http.end();
  fixCharRef("&apos;", '\'');
  fixCharRef("&amp;", '&');
  fixCharRef("&quot;", '"');
  fixCharRef("\xe2\x80\x99", '\'');
  fixCharRef("\xe2\x80\x98", '`');
  fixCharRef("\r\n", '\r');
  return count;
}

void updateTime() {
  WiFiClient client;
  Serial.println("SetTime...");
  if(client.connect("google.com", 80)) {
    client.print("HEAD / HTTP/1.1\r\n\r\n");
    while(!client.available()) {
       yield();
    }
    if(client.available() && client.find("Date: ")) {
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
        Serial.println("Done");
    }
  } else {
    Serial.println("Error");
  }
}

//Reset the display driver
void resetDisplay() {
  pinMode(0, OUTPUT);
  delay(10);
  pinMode(0, INPUT);
}

void loadMsg() {
  Serial.println("loadMsg");
  HTTPClient http;
  http.begin("http:\/\/dolenle.com/led/getMsg.php?key=URl33tHAXORlol4lyef");
  int httpCode = http.GET();
  if(httpCode == HTTP_CODE_OK && line < MAX_LINES) {
    String payload = http.getString();
    Serial.println(payload);
    payload.toCharArray(headlines[line++], sizeof(headlines[0]));
  } else {
    Serial.println("Error");
  }
  http.end();
  fixCharRef("\r\n", '\r');
}

int updateFeeds() {
  line = 0;
  for(int i = 0; i < numFeeds && line < MAX_LINES; i++) {
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

//MTA website sucks
void loadMTAServiceStatus() {
  WiFiClient client;
  Serial.print("MTAService...");
  bool secondLine = false;
  if(line < MAX_LINES-2 && client.connect("web.mta.info", 80)) {
    //create http request
    client.print("GET /status/serviceStatus.txt HTTP/1.1\r\nHost: web.mta.info\r\nConnection: close\r\n\r\n");
    while (!client.available()) {
      yield();
    }
    char* intro = "Subway Status";
    int index = strlen(intro); //next free index
    strcpy(headlines[line], intro);
    headlines[line][index++] = '\r'; //line separator
    while(client.available() && client.findUntil("<name>", "</subway>")) {
      String txt = client.readStringUntil('<');
      headlines[line][index++] = '(';
      txt.toCharArray(headlines[line]+index, sizeof(headlines)-index);
      index += txt.length();
      headlines[line][index++] = ')';
      strcpy(headlines[line]+index, " - ");
      index += 3;
      client.find("<status>");
      txt = client.readStringUntil('<');
      txt.toLowerCase();
      txt.toCharArray(headlines[line]+index, sizeof(headlines)-index);
      index += txt.length();
      strcpy(headlines[line]+index, "    ");
      index += 4;
      if(index > 120 && !secondLine) { //nearing end
        line++;
        index = 0;
        secondLine = true;
      }
    }
    line++;
    Serial.println("Done");
    client.stop();
  } else {
    Serial.println("Error"); //connect failed or not enough buffer space
  }
}

//Main server page
void pageHandler() {
  if(server.args()) {
    if(server.hasArg("mta")) {
      loadMTAServiceStatus();
    } else if(server.hasArg("clk")) {
      updateTime();
    } else if(server.hasArg("rst")) {
      resetDisplay();
    } else if(server.hasArg("get")) {
      loadMsg();
    }
    server.sendContent(redirHeader);
    return;
  }
  String content = "<html><head><title>web-scroller</title><style>body{font-family:Helvetica,Arial,Sans-Serif;}</style></head><body><h1>ESP8266 web-scroller</h1><h4><a href='/?clk=1'>Update Clock</a> | <a href='/?rst=1'>Reset Display</a> | <a href='/?get=1'>Get Message</a> | <a href='/?mta=1'>Get Subway Status</a></h4><form action='/msg' method='POST'>Temporary Message:<input name='msg' size=90> <input type='submit'></form>Animation:<form action='/effect'>Enter: <select name='enter'>";
  content += effectSelect;
  content += " Exit: <select name='exit'>";
  content += effectSelect;
  content += " Speed: <input type=text size=4 name='spd' value=2> Scroll Speed: <input type=text size=4 name='scr' value=20><input type=submit></form>Loaded:<ol>";
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
  content += "<input type='submit'></form>WiFi Settings:<form action='/wifi' method='POST'>SSID:<input name='st_ssid' value='"+String(st_ssid)+"'> PASS:<input type='password' name='st_pass' placeholder='*****'> AP SSID:<input name='ap_ssid' value='"+String(ap_ssid)+"'> <input type='submit'></form>Intervals (ms):<form action='/int'>Feed:<input name='rss' size=9 value="+String(rss_interval)+"> Clock:<input name='time' size=9 value="+String(clock_interval)+"> Display:<input name='disp' size=9 value="+String(disp_interval)+"><input type='submit'></form><form method='POST' action='/ota' enctype='multipart/form-data'>Firmware: <input type='file' name='update'><input type='submit'></form></body></html>";
  server.send(200, "text/html", content);
}

//Manually add a message
void msgHandler() {
  if(server.hasArg("msg")) {
    if(line < MAX_LINES) {
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

//Update WiFi Settings
void wifiHandler() {
  if(server.hasArg("st_ssid") && server.hasArg("st_pass") && server.hasArg("ap_ssid")) {
    if(server.arg("st_pass").length()) { //update only if password entered
      server.arg("st_ssid").toCharArray(st_ssid, sizeof(st_ssid));
      server.arg("st_pass").toCharArray(st_pass, sizeof(st_pass));
    }
    server.arg("ap_ssid").toCharArray(ap_ssid, sizeof(ap_ssid));
    WiFi.disconnect();
    WiFi.begin(st_ssid, st_pass);
    WiFi.softAP(ap_ssid);
    while (WiFi.status() != WL_CONNECTED) {
      server.handleClient();
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

//Change text animation
void effectHandler() {
  if(server.hasArg("enter") && server.hasArg("exit") && server.hasArg("spd")  && server.hasArg("scr")) {
    Serial1.write(1);
    Serial1.print(server.arg("enter") + ' ' + server.arg("exit") + ' ' + server.arg("spd") + ' ' + server.arg("scr") + ' ');
  }
  server.sendContent(redirHeader);
}

//Firmware update
void otaHandler() {
  HTTPUpload& upload = server.upload();
  if(upload.status == UPLOAD_FILE_START) {
    Serial.setDebugOutput(true);
    Serial.printf("Update: %s\n", upload.filename.c_str());
    uint32_t maxSketchSpace = (ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000;
    if(!Update.begin(maxSketchSpace)){//start with max available size
      Update.printError(Serial);
    }
  } else if(upload.status == UPLOAD_FILE_WRITE) {
    if(Update.write(upload.buf, upload.currentSize) != upload.currentSize){
      Update.printError(Serial);
    }
  } else if(upload.status == UPLOAD_FILE_END) {
    if(Update.end(true)){ //true to set the size to the current progress
      Serial.printf("Update Success: %u\nRebooting...\n", upload.totalSize);
    } else {
      Update.printError(Serial);
    }
    Serial.setDebugOutput(false);
  }
  yield();
}


