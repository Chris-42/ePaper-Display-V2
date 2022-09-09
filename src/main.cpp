#include <Arduino.h>

#include <WiFi.h>
#include <ESPmDNS.h>
#include <WiFiUdp.h>
#include <HTTPClient.h>
//#include <ESP32httpUpdate.h>
//#include <esp_https_ota.h>
#include <esp_sleep.h>
#include <ArduinoOTA.h>
#include <Preferences.h>

#define ENABLE_GxEPD2_GFX 0

#define VERSION "0.02"

#include <GxEPD2.h>
#include <GxEPD2_BW.h>
#include <GxEPD2_EPD.h>
#include <GxEPD2_GFX.h>
#include <Fonts/FreeMonoBold9pt7b.h>

#define DIN D10
#define CLK D8 
#define CS D4
#define DC D6
#define BUSY D5
#define RST D7
#define BUTTON D9
#define USB_PWR D3
#define BAT_VOLT D2
#define KEY D1

#define FactorSeconds 1000000ULL

#define IS_USB_CONNECTED digitalRead(USB_PWR)

#define IMAGESIZE (800*480/8+62)

#define MAX_DISPAY_BUFFER_SIZE 50000ul // ~10k is a good compromise, 50k shoult fill all
#define MAX_HEIGHT(EPD) (EPD::HEIGHT <= MAX_DISPAY_BUFFER_SIZE / (EPD::WIDTH / 8) ? EPD::HEIGHT : MAX_DISPAY_BUFFER_SIZE / (EPD::WIDTH / 8))

GxEPD2_BW<GxEPD2_750_T7, MAX_HEIGHT(GxEPD2_750_T7)> display(GxEPD2_750_T7(/*CS=*/ -1, /*DC=*/ DC, /*RST=*/ RST, /*BUSY=*/ BUSY));
//GxEPD2_BW<GxEPD2_750, MAX_HEIGHT(GxEPD2_750)> display(GxEPD2_750(/*CS=*/ -1, /*DC=*/ DC, /*RST=*/ RST, /*BUSY=*/ BUSY));

struct wificonfig_t {
  char ssid[32];
  char password[32];
};

struct serverconfig_t {
  char update_url[128];
  char image_url[128];
  char displayname[32];
  long interval;
};

enum RunType {
  EmptyBat,
  NoConfig,
  SerialConfig,
  SleepForEver,
  NoServerConfig,
  NoHttpResponse,
  Normal
};

struct imagedata_t {
  uint8_t *image;
  uint32_t sleep;
  uint32_t image_size;
  float newVersion;
  char contenthash[34];
};

RTC_DATA_ATTR char rtc_contenthash[34];
RTC_DATA_ATTR uint32_t rtc_image_time;

Preferences preferences;
struct wificonfig_t wificonfig;
struct serverconfig_t serverconfig;

bool in_otau = false;
uint32_t wifi_start = 0;
uint32_t setup_fin = 0;
int bat_mV = 0;
int portal_on_time_ms = 180000;
bool is_loading = false;
bool wifi_wait = false;
bool server_is_configured = false;

#define SERIALPRINT(x) if(is_loading) Serial.print(x)
#define SERIALPRINTLN(x) if(is_loading) Serial.println(x)
#define SERIALPRINTF(...) if(is_loading) Serial.printf(__VA_ARGS__)

int get_wlan_config(struct wificonfig_t *wifi) {
  if(!preferences.begin("wificonfig", true)) {
    preferences.end();
    return(0);
  }
  int ret = preferences.getString("ssid", wifi->ssid, sizeof(wifi->ssid));
  if (ret < 1) {
    preferences.end();
    return (0);
  }
  preferences.getString("password", wifi->password, sizeof(wifi->password));
  preferences.end();
  return (1);
}

int get_server_config(struct serverconfig_t *server) {
  if(!preferences.begin("serverconfig", true)) {
    SERIALPRINTLN("no preferences found");
    preferences.end();
    return(0);
  }
  int ret = preferences.getString("image_url", server->image_url, sizeof(server->image_url));
  if (ret < 1) {
    SERIALPRINTLN("no image_url found");
    preferences.end();
    return (0);
  }
  preferences.getString("update_url", server->update_url, sizeof(server->update_url));
  if (ret < 1) {
    SERIALPRINTLN("no update_url found");
    preferences.end();
    return (0);
  }
  preferences.getString("displayname", server->displayname, sizeof(server->displayname));
  server->interval = preferences.getLong("interval");
  if (server->interval < 60) {
    server->interval = 900;
  }
  preferences.end();
  return (1);
}

void draw_bat() {
  int bat_bar = (bat_mV - 3000) / 20; // 3.0 - 4.2 -> 0-1200 normalized to 0-60
  if(bat_bar > 60) {
    bat_bar = 60;
  }
  float bat_float = int(bat_mV / 10); // scale to 3 digit
  bat_float /= 100;
  display.fillRect(490, 460, 310, 20, GxEPD_WHITE);
  display.setFont(&FreeMonoBold9pt7b);
  display.setTextColor(GxEPD_BLACK);
  display.setTextSize(0);
  display.setCursor(400, 479);
  display.print("RSSI ");
  display.print(WiFi.RSSI());
  display.setCursor(500, 479);
  display.print("Ver ");
  display.print(VERSION);
  display.setCursor(622, 479);
  display.print(bat_float);
  display.print("V");
  // bat symbol
  display.drawRect(701, 465, 60, 14, GxEPD_BLACK);
  display.fillRect(760, 469, 6, 6, GxEPD_BLACK);
  display.fillRect(701, 466, bat_bar, 12, GxEPD_BLACK);
}

void draw_flash() {
  display.fillTriangle(780+5, 460, 780, 460+9, 780+5, 460+9, GxEPD_BLACK);
  display.fillTriangle(780+5, 460+7, 780+5, 460+19, 780+10, 460+7, GxEPD_BLACK);
  display.fillTriangle(780+3, 460+16, 780+5, 460+19, 780+7, 460+16, GxEPD_BLACK);
}

// display some info on display
void displayInfo(RunType which) {
  display.setRotation(0);
  display.setFont(&FreeMonoBold9pt7b);
  display.setTextSize(3);
  display.setTextColor(GxEPD_BLACK);
  String txt;
  switch(which) {
    case EmptyBat:
      txt = "Battery empty!";
      break;
    case NoConfig:
      txt = "no WiFi config!";
      break;
    case SerialConfig:
      txt = "ready to config!";
      break;
    case SleepForEver:
      txt = "Sleeping";
      break;
    case NoServerConfig:
      txt = "No Server Config!";
      break;
    case NoHttpResponse:
      txt = "No HTTP Response!";
      break;
      
  }
  int16_t tbx, tby; uint16_t tbw, tbh;
  display.getTextBounds(txt.c_str(), 0, 0, &tbx, &tby, &tbw, &tbh);
  // center the bounding box by transposition of the origin:
  uint16_t x = ((display.width() - tbw) / 2) - tbx;
  uint16_t y = ((display.height() - tbh) / 2) - tby;
  display.setFullWindow();
  display.fillScreen(GxEPD_WHITE);
  display.setCursor(x, y);
  display.print(txt);
  draw_bat();
  if(IS_USB_CONNECTED) {
    draw_flash();
  }
  display.display();
}

#pragma pack(1) // exact fit - no padding
struct bmp_file_header_t {
  uint16_t signature;
  uint32_t file_size;
  uint32_t reserved;
  uint32_t image_offset;
  //uint32_t image_offset;
  //  uint16_t image_offset2;
};

struct bmp_image_header_t {
  uint32_t header_size;
  int32_t image_width;
  int32_t image_height;
  uint16_t color_planes;
  uint16_t bits_per_pixel;
  uint32_t compression_method;
  uint32_t image_size;
  uint32_t horizontal_resolution;
  uint32_t vertical_resolution;
  uint32_t colors_in_palette;
  uint32_t important_colors;
};
#pragma pack() //back to whatever the previous packing mode was

int draw_image(imagedata_t *im) {
  //int drawBMP( int x, int y, uint8_t* data, long image_size) {
  bmp_file_header_t *bmp_file_header = (bmp_file_header_t *)im->image;
  bmp_image_header_t *bmp_image_header = (bmp_image_header_t *)&(im->image)[14];
  if (bmp_file_header->signature != 0x4D42) { // "BM"
    Serial.printf("unknown file type %d\r\n", bmp_file_header->signature);
    return (-1);
  }
  if (bmp_image_header->bits_per_pixel > 4) {
    Serial.println("to much colors");
    return (-2);
  }
  byte bit_per_pixel = bmp_image_header->bits_per_pixel;
  if (bmp_image_header->compression_method != 0) {
    Serial.println("compression not supported");
    return (-3);
  }
  Serial.println("");

  Serial.printf("imagesize: %d\r\n", bmp_file_header->file_size);
  Serial.printf("offset: %d\r\n", bmp_file_header->image_offset);

  int width = bmp_image_header->image_width;
  int height = bmp_image_header->image_height;
  if (height < 0) {
    height = 0 - height;
  }
  Serial.printf("%dx%d/%d at %d (%d)\r\n", width, height, bit_per_pixel, bmp_file_header->image_offset, bmp_file_header->file_size);
  delay(20);
  /*if(bit_per_pixel == 1) {
    //display.writeImage(&data[bmp_file_header->image_offset], 0, 0, width, height);
    display.writeImage(&(im->image[bmp_file_header->image_offset]), NULL, 0, 0, width, height, false, false, false);
    display.display();
    return(0);
  } else if(bit_per_pixel == 4){
    display.writeNative(&(im->image[bmp_file_header->image_offset]), NULL, 0, 0, width, height, false, false, false);
    return(0);
  }*/
  display.setFullWindow();
  int py = height;
  int y_step = -1;
  if (bmp_image_header->image_height < 0) {
    py = 0;
    y_step = 1;
  }
  uint8_t *ptr = &(im->image)[bmp_file_header->image_offset];
  int px = 0;
  int b = 0;
  int i = 0;
  while (i < width * height) {
    if ((px >= 0) && (px < display.width()) && (py >= 0) && (py < display.height())) {
      byte color = (ptr[0] & (1 << (7 - b))) >> (7 - b);
      if(bit_per_pixel == 2) {
        color = (ptr[0] & (3 << (7 - b))) >> (7 - b);
      }
      switch(color) {
        case 0: 
          display.drawPixel(px, py, GxEPD_BLACK);
          break;
        case 1: 
          display.drawPixel(px, py, GxEPD_WHITE);
          break;
        case 2: 
          display.drawPixel(px, py, GxEPD_RED);
          break;
        case 3: 
          display.drawPixel(px, py, GxEPD_LIGHTGREY);
          break;
      }
/*      
        if (ptr[0] & (1 << (7 - b))) {
          display.drawPixel(px, py, GxEPD_WHITE);
        } else {
          display.drawPixel(px, py, GxEPD_BLACK);
        }
      */
    }
    b += bit_per_pixel;
    if (b == 8) {
      b = 0;
      ptr++;
    }
    px++;
    if (px == width) {
      px = 0;
      py += y_step;
    }
    i++;
  }
  return(0);
}

void go_to_sleep(uint32_t seconds) {
  if(seconds) {
    esp_sleep_enable_timer_wakeup(FactorSeconds * seconds);
  }
  if(is_loading) {
    //esp_deep_sleep_enable_gpio_wakeup(BIT(KEY), ESP_GPIO_WAKEUP_GPIO_HIGH);
  } else {
    esp_deep_sleep_enable_gpio_wakeup(BIT(KEY) | BIT(USB_PWR), ESP_GPIO_WAKEUP_GPIO_HIGH);
  }
  //rtc_run_millis += millis();
  esp_deep_sleep_start();
}

// OTA update task
void update_loop(void*z) {
  while (1) {
    ArduinoOTA.handle();
    if(!digitalRead(BUTTON)) {
      ESP.restart();
    }
    if(!in_otau) {
      vTaskDelay(5);
    }
	}
}

void process_cmd(String &cmd) {
  Serial.println("");
  if(cmd == "restart") {
    ESP.restart();
  } else if(cmd.startsWith("ssid ")) {
    strcpy(wificonfig.ssid, cmd.substring(5).c_str());
  } else if(cmd.startsWith("pw ")) {
    strcpy(wificonfig.password, cmd.substring(3).c_str());
  } else if(cmd.startsWith("name ")) {
    strcpy(serverconfig.displayname, cmd.substring(5).c_str());
  } else if(cmd.startsWith("url ")) {
    strcpy(serverconfig.image_url, cmd.substring(4).c_str());
  } else if(cmd.startsWith("wake ")) {
    serverconfig.interval = cmd.substring(5).toInt();
  } else if(cmd.startsWith("update ")) {
    strcpy(serverconfig.update_url, cmd.substring(7).c_str());
  } else if(cmd == "show") {
    Serial.print("AP: ");
    Serial.println(wificonfig.ssid);
    Serial.print("PW: ");
    Serial.println(wificonfig.password);
    Serial.print("IP: ");
    Serial.println(WiFi.localIP());
    Serial.print("Name: ");
    Serial.println(serverconfig.displayname);
    Serial.print("URL: ");
    Serial.println(serverconfig.image_url);
    Serial.print("Wake Interval: ");
    Serial.println(serverconfig.interval);
    Serial.print("Update URL: ");
    Serial.println(serverconfig.update_url);
  } else if(cmd == "save") {
    if(!preferences.begin("wificonfig")) {
      Serial.println("unable to open Preferences for wifi config");
    } else {
      int x = preferences.putString("ssid", wificonfig.ssid);
      //int x = preferences.putBytes("ssid", wifi.ssid, strlen(wifi.ssid));
      Serial.printf("stored ssid len %d\r\n", x);
      x = preferences.putString("password", wificonfig.password);
      Serial.printf("stored passwd len %d\r\n", x);
      preferences.end();
    }
    if(!preferences.begin("serverconfig")) {
      Serial.println("unable to open Preferences for serverconfig");
    } else {
      int x = preferences.putString("displayname", serverconfig.displayname);
      Serial.printf("stored name len %d\r\n", x);
      x = preferences.putString("image_url", serverconfig.image_url);
      Serial.printf("stored url len %d\r\n", x);
      x = preferences.putString("update_url", serverconfig.update_url);
      Serial.printf("stored update len %d\r\n", x);
      x = preferences.putLong("interval", serverconfig.interval);
      Serial.printf("stored interval %d\r\n", x);
      preferences.end();
    }
  } else if(cmd == "help") {
    Serial.println("ssid <str>");
    Serial.println("pw <str>");
    Serial.println("name <str>");
    Serial.println("url <uri>");
    Serial.println("wake <int>");
    Serial.println("update <uri>");
    Serial.println("save");
    Serial.println("restart");
    Serial.println("");
  } else {
    Serial.println(cmd);
    Serial.println(" unknown (help?)");
  }
  cmd = "";
}

// serial config task
void serial_config(void*z) {
  String cmd = "";
  while (1) {
    if(Serial.available()) {
      char c = Serial.read();
      if((c == '\r') || (c == '\n')) {
        process_cmd(cmd);
      } else if(c == 8) { // backspace
        cmd.remove(cmd.length() - 1);
        Serial.print(c);
        Serial.print(" ");
        Serial.print(c);
      } else {
        Serial.print(c);
        cmd += c;
      }
    }
    vTaskDelay(5);
	}
}

//try to get update from server, auto restart if done, else return
/*
int get_update(struct serverconfig_t *server) {
  int ok = 0;
  Serial.println("check for update");
  Serial.println(server->update_url);
  if (!strlen(server->update_url)) {
    return(0);
  }
  t_httpUpdate_return ret = ESPhttpUpdate.update(server->update_url, VERSION);
  switch (ret) {
    case HTTP_UPDATE_FAILED: 
      Serial.printf("HTTP_UPDATE_FAILD Error (%d): %s\n", ESPhttpUpdate.getLastError(), ESPhttpUpdate.getLastErrorString().c_str());
      break;

    case HTTP_UPDATE_NO_UPDATES:
      Serial.println("HTTP_UPDATE_NO_UPDATES");
      ok = 1;
      break;

    case HTTP_UPDATE_OK:
      Serial.println("HTTP_UPDATE_OK");
      ok = 1;
      break;
  }
  return(ok);
}*/

int fetch_image(struct imagedata_t *im) {
  Serial.println("check for image");
  Serial.println(serverconfig.image_url);
  int tries = 3;
  int got_response = 0;
  char image_time_str[32];
  char next_interval_str[32];
  char voltage[16];
  HTTPClient http;
  im->sleep = 60;
  //time_t image_time_tm = im->image_time;
  //time_t interval_time_tm = im->next_interval;
  //strftime(image_time_str, sizeof(image_time_str), "%a, %d %b %Y %H:%M:%S GMT", gmtime(&image_time_tm));
  //Serial.print("Image time was:");
  //Serial.println(image_time_str);
  //if (!im->next_interval) {
    // if not send by server it defaults to this set in config
  //  interval_time_tm = (int(((now + 10) / server->interval)) + 1 ) * server->interval;
  //}
  //strftime(next_interval_str, sizeof(next_interval_str), "%a, %d %b %Y %H:%M:%S GMT", gmtime(&interval_time_tm));

  while (tries) {
    tries--;
    http.begin(serverconfig.image_url);
    const char* headerNames[] = {"Content-Length", "ContentHash", "Sleep", "NewVersion", "Version"};
    http.collectHeaders(headerNames, sizeof(headerNames) / sizeof(headerNames[0]));
    sprintf(voltage, "%d", bat_mV);
    // If-Modified-Since: Wed, 21 Oct 2015 07:28:00 GMT
    //http.addHeader("If-Modified-Since", image_time_str);
    http.addHeader("DisplayName", serverconfig.displayname);
    http.addHeader("Version", VERSION);
    http.addHeader("ContentHash", rtc_contenthash);
    //http.addHeader("Nextinterval", next_interval_str);
    http.addHeader("BatteriePower", voltage);
    //http.addHeader("Wakeup", wakeup_by.c_str());
    int httpCode = http.GET();
    if (httpCode > 0) {
      // HTTP header has been send and Server response header has been handled
      got_response = 1;
      SERIALPRINTF("[HTTP] GET... code: %d\r\n", httpCode);
      if (httpCode == 304) {
        return (2);
      }
      // set date based on server time
      //char date[50];
      //http.header("Date").toCharArray(date, sizeof(date));
      //now = convert_datestr2time(date);
      //struct timeval tv;
      //tv.tv_sec = now;
      //tv.tv_usec = 0;
      //struct timezone tz = {0};
      //settimeofday(&tv, NULL);
      int content_len = http.getSize();
      if(content_len == -1) {
        content_len = IMAGESIZE;
      }
      SERIALPRINTLN("Content-Length: " + http.header("Content-Length"));
      SERIALPRINTLN("Sleep: " + http.header("Sleep"));
      //long image_size = http.header("Content-Length").toInt();
      //http.header("Nextinterval").toCharArray(date, 50);
      im->newVersion = http.header("NewVersion").toFloat();
      im->sleep = http.header("Sleep").toInt();;
      if (!im->sleep) {
        // if not send by server it defaults to this set in config
        im->sleep = serverconfig.interval;
      }
      if (httpCode == HTTP_CODE_OK) {
        Serial.println("load the image");
        WiFiClient w = http.getStream();
        int s = 0;
        int sz = 0;
        delay(10);
        im->image = (uint8_t*)malloc(IMAGESIZE);
        uint8_t *imptr = im->image;
        //s = w.read(imptr, IMAGESIZE - s);
        s = w.readBytes(imptr, IMAGESIZE - s);
        sz += s;
        SERIALPRINTF("read %d(%d of %d)\r\n", s, sz, content_len);
        imptr += s;
        int receive_tries = 15;
        while(http.connected() && ((s > 0) || receive_tries)) {
          if (s <= 0) {
            delay(50);
            receive_tries--;
          }
          //s = w.read(imptr, sizeof(*im->image) - s);
          s = w.readBytes(imptr, IMAGESIZE - s);
          if (s > 0) {
            sz += s;
            imptr += s;
            receive_tries = 15;
          }
          SERIALPRINTF("read %d(%d of %d)\r\n", s, sz, content_len);
          if (sz >= content_len) {
            receive_tries = 0;
          }
        }
        im->image_size = sz;
        Serial.printf("read sum %d (%d)\r\n", sz, content_len);
        if (sz >= content_len) {
          http.header("ContentHash").toCharArray(im->contenthash, sizeof(im->contenthash));
          return (1);
        }
      }
      SERIALPRINTF("[HTTP] GET... failed, error: %s\n", http.errorToString(httpCode).c_str());
      break;
    } else {
      SERIALPRINTLN("[HTTP] got no response");
    }
  }
  if(!got_response) {
    displayInfo(NoHttpResponse);
    rtc_contenthash[0] = '\0';
  }
  return (0);
}

void WiFiGotIP(WiFiEvent_t event, WiFiEventInfo_t info) {
  SERIALPRINTLN("WiFi connected");
  //Serial.printf("WiFiIP: %lu\r\n", millis());
  SERIALPRINT("IP address: ");
  SERIALPRINTLN(IPAddress(info.got_ip.ip_info.ip.addr));
  SERIALPRINT("Hostname: ");
  SERIALPRINTLN(WiFi.getHostname());
  wifi_wait = false;
  //  local_IP = info.got_ip.ip_info.ip.addr;
  //  subnet = info.got_ip.ip_info.netmask.addr;
  //  gateway = info.got_ip.ip_info.gw.addr;
  if(server_is_configured) {
    imagedata_t image;
    int ret = fetch_image(&image);
    if(ret == 1) {
      draw_image(&image);
      SERIALPRINTLN("draw done");
      draw_bat();
      if(IS_USB_CONNECTED) {
        draw_flash();
      }
      strcpy(rtc_contenthash, image.contenthash);
      display.display();
      digitalWrite(RST, LOW);
    } else if(ret == 2) {
      SERIALPRINTLN("no change");
    } else {
      displayInfo(NoHttpResponse);
    }
    Serial.printf("sleep %d seconds\r\n", image.sleep);
    if(!is_loading) {
      go_to_sleep(image.sleep);
    }
  } else {
    displayInfo(NoServerConfig);
  }
}

void setup() {
  setCpuFrequencyMhz(80);
  pinMode(USB_PWR, INPUT);
  digitalWrite(RST, HIGH);
  pinMode(RST, OUTPUT);
  digitalWrite(CS, HIGH);
  pinMode(CS, OUTPUT);
  digitalWrite(CLK, HIGH);
  pinMode(CLK, OUTPUT);
  pinMode(BUTTON, INPUT_PULLUP);
  pinMode(KEY, INPUT_PULLDOWN);
  pinMode(BAT_VOLT, ANALOG);
  if(IS_USB_CONNECTED) {
    Serial.begin(115200);
    Serial.println("Init");
    portal_on_time_ms = 7200000; // just set it to 2 hour if on USB
    is_loading = true;
  }
  //esp_sleep_wakeup_cause_t wakeup_reason;
  //wakeup_reason = esp_sleep_get_wakeup_cause();
  analogReadResolution(12);
  analogSetAttenuation(ADC_11db);
  bat_mV = analogRead(BAT_VOLT) / 0.716; // 2973 / 4.15V
  if(digitalRead(KEY)) {
    delay(1000);
    if(digitalRead(KEY)) {
      rtc_contenthash[0] = '\0';
    }
  }
  if(!is_loading && bat_mV < 3200) {
    displayInfo(EmptyBat);
    go_to_sleep(0);
  }
  // check if we have wifi credentials
  wificonfig.ssid[0] = '\0';
  if (get_wlan_config(&wificonfig)) {
    WiFi.mode(WIFI_STA);
    WiFi.begin(wificonfig.ssid, wificonfig.password);
    //WiFi.setHostname(sys_config.hostname);
    //WiFi.onEvent(WiFiGotIP, WiFiEvent_t::SYSTEM_EVENT_STA_GOT_IP);
    WiFi.onEvent(WiFiGotIP, WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_GOT_IP);
    wifi_start = millis();
    wifi_wait = true;
  }
  if (!get_server_config(&serverconfig)) {
    displayInfo(NoServerConfig);
  } else {
    server_is_configured = true;
  }
// TODO
  if(wificonfig.ssid[0] && IS_USB_CONNECTED) {
    ArduinoOTA
      .onStart([]() {
        String type;
        if (ArduinoOTA.getCommand() == U_FLASH)
          type = "sketch";
        else // U_SPIFFS
          type = "filesystem";

        // NOTE: if updating SPIFFS this would be the place to unmount SPIFFS using SPIFFS.end()
        Serial.println("Start updating " + type);
        in_otau = true;
      })
      .onEnd([]() {
        Serial.println("\nEnd");
      })
      .onProgress([](unsigned int progress, unsigned int total) {
        SERIALPRINTF("Progress: %u%%\r", (progress / (total / 100)));
      })
      .onError([](ota_error_t error) {
        Serial.printf("Error[%u]: ", error);
        if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
        else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
        else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
        else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
        else if (error == OTA_END_ERROR) Serial.println("End Failed");
        in_otau = false;
      });

    ArduinoOTA.begin();
    Serial.println("OTAU Ready");
  }

  //SPI.end(); // release standard SPI pins, e.g. SCK(18), MISO(19), MOSI(23), SS(5)
  //SPI: void begin(int8_t sck=-1, int8_t miso=-1, int8_t mosi=-1, int8_t ss=-1);
  SPI.begin(CLK, DIN, DIN, CS); // map and init SPI pins SCK(13), MISO(12), MOSI(14), SS(15)
  SPI.setHwCs(true);
  //SPI.setFrequency(10000);
  delay(100);
  SERIALPRINTLN("init epd");
  digitalWrite(RST, HIGH);
  display.init();
  //pinMode(BUSY, INPUT_PULLUP);
  SERIALPRINTLN("init epd done");
  if(IS_USB_CONNECTED) {
    xTaskCreate(update_loop, "update", 4096, NULL, 10, NULL);
    xTaskCreate(serial_config, "serial_config", 4096, NULL, 10, NULL);
    //displayInfo(SerialConfig);
  }
  setup_fin = millis();
}

uint32_t last_run = 0;
uint32_t last_send = 0;

void loop() {
  if(in_otau) {
    return;
  }
  uint32_t ti = millis();
  if(wifi_wait && ((ti - wifi_start) > 20000)) {
    SERIALPRINTLN("WiFi Connection Failed! Delayed Rebooting...");
    go_to_sleep(10);
  }
  //if(digitalRead(KEY)) {
  //  go_to_sleep(10);
    //ESP.restart();
  //}
  if(is_loading && !IS_USB_CONNECTED) {
    ESP.restart();
  }
  if(!is_loading && (ti > (setup_fin + 600000))) {
    displayInfo(SleepForEver);
    go_to_sleep(0);
  }
}