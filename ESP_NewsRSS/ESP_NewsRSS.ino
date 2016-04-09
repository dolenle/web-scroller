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
#define RSS_INTERVAL 600000
#define CLOCK_INTERVAL 3600000
#define DISPLAY_INTERVAL 12000

char headlines[MAX_LINES][MAX_LENGTH];
int line;
unsigned long lastDisp;
unsigned long lastUpdate = -RSS_INTERVAL;
unsigned long lastTimeSet = -CLOCK_INTERVAL;
char* monthTable PROGMEM = "Jan Feb Mar Apr May Jun Jul Aug Sep Oct Nov Dec";
char* redirHeader PROGMEM = "HTTP/1.1 301 OK\r\nLocation: /\r\nCache-Control: no-cache\r\n\r\n";

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
  if(now-lastUpdate > RSS_INTERVAL) {
    updateFeeds();
    loadMsg();
    lastUpdate = now;
  } else if(now-lastTimeSet > CLOCK_INTERVAL) {
    updateTime();
    lastTimeSet = now;
    Serial.println("TimeSet");
  } else {
    static int i;
    if(now-lastDisp > DISPLAY_INTERVAL) {
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
      for (schar += refLen; *schar != 0; schar++) {
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
              src=7-(c-ind);
              continue;
            }
          }
        }
        if (copying) {
          for (int i = src; i < c; i++) {
            if (buff[i] != '<') {
              headlines[line][dest++] = buff[i]; //copy char
              if(buff[i] == '\n') dest--; //skip newlines
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
        if (line > MAX_LINES || count >= maxLines) {
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
    if (client.read() == 't' && client.read() == 'e' && client.read() == ':') { //Date:
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
  if(httpCode == HTTP_CODE_OK && line < MAX_LINES) {
    String payload = http.getString();
    Serial.println(payload);
    payload.toCharArray(headlines[line++], payload.length());
  } else {
    Serial.println("Error");
  }
  http.end();
  server.sendContent(redirHeader);
}

int updateFeeds() {
  line = 0;
  for(int i = 0; i < numFeeds && line < MAX_LINES; i++) {
    loadFeed(feedURLs[i], feedSkip[i], feedLines[i]);
  }
  Serial.print(line);
  Serial.println(" titles loaded:");
  for (int i = 0; i < line; i++) {
    Serial.print(i);
    Serial.print(' ');
    Serial.println(headlines[i]);
  }
  Serial.println();
  return line;
}

//Main server page
void pageHandler() {
  String content = "<html><body><h1>ESP8266 web-scroller</h1><form action='/msg' method='POST'>Force Msg:<input type='text' name='msg' size=90> <input type='submit'></form>WiFi Settings:<form action='/wifi' method='POST'>SSID:<input type='text' name='st_ssid' value='";
  content += String(st_ssid)+"'> PASS:<input type='password' name='st_pass' placeholder='*****'> AP SSID:<input type='text' name='ap_ssid' value='"+String(ap_ssid)+"'> <input type='submit'></form><form action='/feed' method='POST'>Loaded: (";
  content += (millis() - lastUpdate)/1000;
  content += " sec ago)<ol>";
  int i;
  for (int i = 0; i < line; i++) {
    content += "<li>";
    content += headlines[i];
    content += "</li>";
  }
  content += "</ol>Feeds:<br>";
  for(i = 0; (numFeeds < MAX_FEEDS && i <= numFeeds) || (numFeeds == MAX_FEEDS && i < numFeeds); i++) {
    content += "URL:<input type='text' name='url"+String(i)+"' size=80 value='";
    content += feedURLs[i];
    content += "'> Skip:<input type='text' name='skip"+String(i)+"' size=3 value='"+String(feedSkip[i])+"'> Max:<input type='text' name='max"+String(i)+"' size=3 value='"+String(feedLines[i])+"'><br>";
  }
  content += "<input type='submit'></form><a href='/clock'>Update Clock</a><br><a href='/reset'>Reset Display</a><br><a href='/get'>Get Message</a></body></html>";
  server.send(200, "text/html", content);
}

//Immediately and temporarily write a message to the display
void msgHandler() {
  if (server.hasArg("msg")) {
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
  if (server.hasArg("st_ssid") && server.hasArg("st_pass") && server.hasArg("ap_ssid")) {
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

