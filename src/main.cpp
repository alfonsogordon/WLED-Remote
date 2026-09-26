#include <Arduino.h>
#include <M5Unified.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <HTTPClient.h>
#include <ESPmDNS.h>
#include <ArduinoJson.h>

#ifndef WLED_REMOTE_VERSION
#define WLED_REMOTE_VERSION "dev"
#endif

struct WledDevice {
  String name;
  IPAddress ip;
  bool online=false, on=false;
  uint8_t brightness=0;
  int timerMinutes=0;
};

enum class Screen { HOME, DEVICE, TIMER, ALL };
static constexpr size_t MAX_DEVICES=16;
WledDevice devices[MAX_DEVICES];
size_t deviceCount=0, selected=0;
int menuIndex=0;
Screen screen=Screen::HOME;
unsigned long lastRefresh=0;

const char* actions[]={"POWER","WHITE","DEFAULT","BRIGHT +","BRIGHT -","SLEEP TIMER","BACK"};
const int ACTION_COUNT=7;
const int timers[]={30,60,120,180,0};
const char* timerLabels[]={"30 MIN","1 HOUR","2 HOURS","3 HOURS","CANCEL"};

void backdrop(){
  M5.Display.fillScreen(M5.Display.color565(5,6,16));
}

void title(const String& right=""){
  M5.Display.setTextDatum(top_left);
  M5.Display.setTextColor(TFT_CYAN);
  M5.Display.setTextSize(2);
  M5.Display.drawString("WLED REMOTE",7,5);
  M5.Display.setTextSize(1);
  M5.Display.setTextColor(TFT_LIGHTGREY);
  if(right.length()) M5.Display.drawRightString(right,233,9);
}

bool queryDevice(WledDevice& d){
  HTTPClient http; http.setTimeout(1000);
  if(!http.begin("http://"+d.ip.toString()+"/json")) return false;
  int code=http.GET();
  if(code!=200){http.end();d.online=false;return false;}
  JsonDocument doc;
  auto err=deserializeJson(doc,http.getStream()); http.end();
  if(err){d.online=false;return false;}
  d.name=doc["info"]["name"]|d.name;
  d.on=doc["state"]["on"]|false;
  d.brightness=doc["state"]["bri"]|0;
  bool nl=doc["state"]["nl"]["on"]|false;
  d.timerMinutes=nl ? (int)(doc["state"]["nl"]["rem"]|0) : 0;
  d.online=true; return true;
}

bool sendState(WledDevice& d,const String& json){
  HTTPClient http; http.setTimeout(1400);
  if(!http.begin("http://"+d.ip.toString()+"/json/state")) return false;
  http.addHeader("Content-Type","application/json");
  int code=http.POST(json); http.end();
  if(code<200||code>=300) return false;
  delay(60); return queryDevice(d);
}
void toggle(WledDevice& d){sendState(d,d.on?"{\"on\":false}":"{\"on\":true}");}
void white(WledDevice& d){sendState(d,"{\"on\":true,\"seg\":[{\"fx\":0,\"col\":[[255,255,255]]}]}");}
void defaults(WledDevice& d){sendState(d,"{\"on\":true,\"ps\":1}");}
void brightness(WledDevice& d,int delta){
  int b=constrain((int)d.brightness+delta,5,255);
  sendState(d,"{\"on\":true,\"bri\":"+String(b)+"}");
}
void timer(WledDevice& d,int mins){
  if(mins==0) sendState(d,"{\"nl\":{\"on\":false}}");
  else sendState(d,"{\"on\":true,\"nl\":{\"on\":true,\"dur\":"+String(mins)+",\"mode\":0,\"tbri\":0}}");
}
template<typename F> void all(F fn){for(size_t i=0;i<deviceCount;i++) if(devices[i].online) fn(devices[i]);}

bool addCandidate(IPAddress ip,const String& fallbackName="WLED"){
  if(deviceCount>=MAX_DEVICES || !ip || ip==WiFi.localIP()) return false;
  for(size_t j=0;j<deviceCount;j++) if(devices[j].ip==ip) return false;
  WledDevice d; d.ip=ip; d.name=fallbackName;
  if(queryDevice(d)){devices[deviceCount++]=d; return true;}
  return false;
}

void discover(){
  deviceCount=0;

  // Fast path: standard WLED mDNS advertisement.
  int n=MDNS.queryService("wled","tcp");
  for(int i=0;i<n && deviceCount<MAX_DEVICES;i++)
    addCandidate(MDNS.IP(i),MDNS.hostname(i));

  // Fallback: probe the local /24 for WLED JSON endpoints. This catches
  // devices that do not appear in the ESP32 mDNS service query.
  IPAddress local=WiFi.localIP(), mask=WiFi.subnetMask();
  if(mask[0]==255 && mask[1]==255 && mask[2]==255 && mask[3]==0){
    M5.Display.fillScreen(M5.Display.color565(5,6,16)); title("SCAN");
    M5.Display.setTextColor(TFT_WHITE); M5.Display.setTextSize(1);
    M5.Display.drawString("Finding all WLED lights...",10,54);
    for(int host=1;host<255 && deviceCount<MAX_DEVICES;host++){
      IPAddress ip(local[0],local[1],local[2],host);
      addCandidate(ip);
      if((host%16)==0){
        M5.Display.fillRect(10,80,220,12,M5.Display.color565(5,6,16));
        M5.Display.drawString(String(host)+"/254   "+String(deviceCount)+" found",10,80);
      }
      M5.update();
    }
  }
  if(selected>deviceCount) selected=0;
}

void drawHome(){
  backdrop(); title(WiFi.isConnected()?"WIFI":"OFFLINE");
  M5.Display.setTextSize(1);
  M5.Display.setTextColor(selected==0?TFT_YELLOW:TFT_WHITE);
  M5.Display.drawString(selected==0?"> ALL LIGHTS":"  ALL LIGHTS",8,30);
  int first=0;
  if(selected>4) first=(int)selected-4;
  int visible=min((int)deviceCount-first,4);
  for(int row=0;row<visible;row++){
    int i=first+row;
    int y=48+row*18; size_t idx=i+1;
    String nm=devices[i].name; if(nm.length()>17) nm=nm.substring(0,17);
    M5.Display.setTextColor(selected==idx?TFT_YELLOW:TFT_WHITE);
    M5.Display.drawString(String(selected==idx?"> ":"  ")+nm,8,y);
    M5.Display.setTextColor(devices[i].on?TFT_GREEN:TFT_DARKGREY);
    M5.Display.drawRightString(devices[i].on?"ON":"OFF",231,y);
  }
  M5.Display.setTextColor(TFT_LIGHTGREY);
  M5.Display.drawString(deviceCount?String(deviceCount)+" LIGHTS  A OPEN  B NEXT":"NO LIGHTS - HOLD B RESCAN",8,123);
}

void drawActionMenu(bool isAll){
  backdrop(); title(isAll?"ALL LIGHTS":devices[selected-1].name);
  for(int i=0;i<ACTION_COUNT;i++){
    int y=30+i*14;
    M5.Display.setTextColor(i==menuIndex?TFT_YELLOW:TFT_WHITE);
    M5.Display.drawString(String(i==menuIndex?"> ":"  ")+actions[i],9,y);
  }
  M5.Display.setTextColor(TFT_LIGHTGREY);
  M5.Display.drawString("A SELECT  B NEXT  HOLD B BACK",8,123);
}

void drawTimer(){
  backdrop(); title("SLEEP");
  for(int i=0;i<5;i++){
    int y=35+i*17;
    M5.Display.setTextColor(i==menuIndex?TFT_YELLOW:TFT_WHITE);
    M5.Display.drawString(String(i==menuIndex?"> ":"  ")+timerLabels[i],18,y);
  }
}

void render(){
  if(screen==Screen::HOME) drawHome();
  else if(screen==Screen::TIMER) drawTimer();
  else drawActionMenu(screen==Screen::ALL);
}

void runAction(bool isAll){
  auto one=[&](auto fn){if(isAll) all(fn); else fn(devices[selected-1]);};
  switch(menuIndex){
    case 0: one([](WledDevice& d){toggle(d);}); break;
    case 1: one([](WledDevice& d){white(d);}); break;
    case 2: one([](WledDevice& d){defaults(d);}); break;
    case 3: one([](WledDevice& d){brightness(d,32);}); break;
    case 4: one([](WledDevice& d){brightness(d,-32);}); break;
    case 5: screen=Screen::TIMER; menuIndex=1; render(); return;
    case 6: screen=Screen::HOME; menuIndex=0; render(); return;
  }
  render();
}

String wifiPage(const String& message=""){
  String options;
  int n=WiFi.scanNetworks();
  for(int i=0;i<n;i++){
    String ssid=WiFi.SSID(i);
    ssid.replace("&","&amp;"); ssid.replace("<","&lt;"); ssid.replace(">","&gt;");
    options += "<option value=\"" + ssid + "\">" + ssid + " (" + String(WiFi.RSSI(i)) + " dBm)</option>";
  }
  return "<!doctype html><html><head><meta name='viewport' content='width=device-width,initial-scale=1'>"
         "<title>WLED Remote Setup</title><style>body{font-family:system-ui;background:#080a18;color:#fff;margin:0;"
         "min-height:100vh;display:grid;place-items:center}.c{width:min(420px,86vw);padding:28px;border-radius:22px;"
         "background:#12172d}h1{color:#63eaff}select,input,button{box-sizing:border-box;width:100%;padding:14px;"
         "margin:8px 0;border-radius:12px;border:1px solid #39436e;background:#090d20;color:#fff}button{background:"
         "#663cff;font-weight:700}.m{color:#ffb95e}</style></head><body><div class='c'><h1>WLED Remote</h1>"
         "<p>Choose the Wi-Fi network used by your WLED lamps.</p><p class='m'>" + message + "</p>"
         "<form method='POST' action='/save'><select name='ssid'>" + options + "</select>"
         "<input name='pass' type='password' placeholder='Wi-Fi password'><button>CONNECT</button></form>"
         "</div></body></html>";
}

void wifiSetup(){
  WiFi.mode(WIFI_STA);
  WiFi.begin();
  M5.Display.fillScreen(TFT_BLACK); title("WIFI");
  M5.Display.setTextColor(TFT_WHITE); M5.Display.setTextSize(1);
  M5.Display.drawString("Connecting to saved Wi-Fi...",10,55);
  unsigned long start=millis();
  while(WiFi.status()!=WL_CONNECTED && millis()-start<10000){delay(100);M5.update();}
  if(WiFi.status()==WL_CONNECTED) return;

  WiFi.disconnect();
  delay(100);
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP("WLED-Remote-Setup");
  IPAddress ap=WiFi.softAPIP();
  DNSServer dns;
  WebServer server(80);
  dns.start(53,"*",ap);

  bool finished=false;
  server.on("/",HTTP_GET,[&](){server.send(200,"text/html",wifiPage());});
  server.on("/generate_204",HTTP_GET,[&](){server.sendHeader("Location","/",true);server.send(302,"text/plain","");});
  server.on("/hotspot-detect.html",HTTP_GET,[&](){server.sendHeader("Location","/",true);server.send(302,"text/plain","");});
  server.onNotFound([&](){server.sendHeader("Location","/",true);server.send(302,"text/plain","");});
  server.on("/save",HTTP_POST,[&](){
    String ssid=server.arg("ssid"), pass=server.arg("pass");
    server.send(200,"text/html","<html><body style='font-family:system-ui;background:#080a18;color:white'><h2>Connecting...</h2><p>You can return to WLED Remote.</p></body></html>");
    delay(250);
    WiFi.begin(ssid.c_str(),pass.c_str());
    unsigned long t=millis();
    while(WiFi.status()!=WL_CONNECTED && millis()-t<15000){delay(100);}
    if(WiFi.status()==WL_CONNECTED) finished=true;
  });
  server.begin();

  M5.Display.fillScreen(TFT_BLACK); title("SETUP");
  M5.Display.setTextColor(TFT_WHITE); M5.Display.setTextSize(1);
  M5.Display.drawString("On your phone connect to:",10,42);
  M5.Display.setTextColor(TFT_CYAN); M5.Display.setTextSize(2);
  M5.Display.drawString("WLED-Remote-Setup",10,59);
  M5.Display.setTextSize(1); M5.Display.setTextColor(TFT_LIGHTGREY);
  M5.Display.drawString("Setup page should open automatically",10,88);
  M5.Display.drawString("or browse to 192.168.4.1",10,103);

  while(!finished){
    dns.processNextRequest(); server.handleClient(); M5.update(); delay(2);
  }
  server.stop(); dns.stop(); WiFi.softAPdisconnect(true); WiFi.mode(WIFI_STA);
  delay(250);
}

void setup(){
  auto cfg=M5.config(); M5.begin(cfg);
  M5.Display.setRotation(1); M5.Display.setBrightness(110);
  wifiSetup();
  if(WiFi.status()!=WL_CONNECTED){M5.Display.fillScreen(TFT_BLACK);title("NO WIFI");return;}
  MDNS.begin("wled-remote");
  discover(); render();
}

void loop(){
  M5.update();
  if(WiFi.status()!=WL_CONNECTED){delay(30);return;}

  if(M5.BtnB.wasHold()){
    if(screen!=Screen::HOME){screen=Screen::HOME;menuIndex=0;}
    else discover();
    render(); delay(180); return;
  }

  if(M5.BtnB.wasPressed()){
    if(screen==Screen::HOME) selected=(selected+1)%(deviceCount+1);
    else if(screen==Screen::TIMER) menuIndex=(menuIndex+1)%5;
    else menuIndex=(menuIndex+1)%ACTION_COUNT;
    render();
  }

  if(M5.BtnA.wasPressed()){
    if(screen==Screen::HOME){
      screen=(selected==0)?Screen::ALL:Screen::DEVICE; menuIndex=0;
    } else if(screen==Screen::TIMER){
      int mins=timers[menuIndex];
      bool isAll=(selected==0);
      if(isAll) all([&](WledDevice& d){timer(d,mins);});
      else timer(devices[selected-1],mins);
      screen=isAll?Screen::ALL:Screen::DEVICE; menuIndex=0;
    } else runAction(screen==Screen::ALL);
    render();
  }

  if(screen==Screen::HOME && millis()-lastRefresh>10000){
    lastRefresh=millis();
    for(size_t i=0;i<deviceCount;i++) queryDevice(devices[i]);
    render();
  }
  delay(15);
}
