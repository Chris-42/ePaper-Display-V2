#include <Arduino.h>

#include <WiFi.h>
#include <ESPmDNS.h>
#include <WiFiUdp.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>
//#include <ESP32httpUpdate.h>
//#include <esp_https_ota.h>
#include <esp_sleep.h>
#include <ArduinoOTA.h>
#include <Preferences.h>
//#include <driver/uart.h>
//#include "driver/usb_serial_jtag.h"
//#include <hal/uart_ll.h>

#define ENABLE_GxEPD2_GFX 0
#define VERSION "0.04"

#include <GxEPD2.h>
#include <GxEPD2_BW.h>
#include <GxEPD2_EPD.h>
#include <GxEPD2_GFX.h>
#include <Fonts/FreeMonoBold9pt7b.h>
#include <PNGdec.h>

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

PNG png;

struct wificonfig_t {
  char ssid[32];
  char password[32];
  bool fake_fixed_ip;
};

struct serverconfig_t {
  char update_url[128];
  char image_url[128];
  char displayname[32];
  long interval;
  uint32_t umillis;
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

enum imagetype_t {UNKNOWN, T_BMP, T_PNG};

struct imagedata_t {
  uint8_t *image;
  uint32_t sleep;
  uint32_t image_size;
  float newVersion;
  char contenthash[34];
  imagetype_t type;
};

RTC_DATA_ATTR char rtc_contenthash[34];
RTC_DATA_ATTR char rtc_image_time[34];
RTC_DATA_ATTR uint32_t rtc_wifi_fail;
RTC_DATA_ATTR bool rtc_wifi_failed;
RTC_DATA_ATTR bool rtc_wifi_channel  = 0;
RTC_DATA_ATTR uint32_t rtc_run_millis;
RTC_DATA_ATTR uint32_t rtc_last_update_check;

RTC_DATA_ATTR IPAddress local_IP = IPADDR_NONE;
RTC_DATA_ATTR IPAddress gateway = IPADDR_NONE;
RTC_DATA_ATTR IPAddress subnet = IPADDR_NONE;
RTC_DATA_ATTR IPAddress dns1 = IPADDR_NONE;
RTC_DATA_ATTR IPAddress dns2 = IPADDR_NONE;

Preferences preferences;
struct wificonfig_t wificonfig;
struct serverconfig_t serverconfig;

String wakeup_by;

bool in_otau = false;
uint32_t wifi_start = 0;
uint32_t wifi_connect_time = 0; //statistic collection
uint32_t setup_fin = 0;
uint32_t next_draw = 0;
int bat_mV = 0;
bool is_loading = false;
bool uart_avail = false;
bool wifi_wait = false;
bool server_is_configured = false;
int rssi = 0;

#define SERIALPRINT(x) if(uart_avail) Serial.print(x)
#define SERIALPRINTLN(x) if(uart_avail) Serial.println(x)
#define SERIALPRINTF(...) if(uart_avail) Serial.printf(__VA_ARGS__)

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
  preferences.getBool("fixed_ip", wifi->fake_fixed_ip);
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
  if (server->interval < 30) {
    server->interval = 30;
  }
  server->umillis = preferences.getLong("umillis");
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
  //display.fillRect(490, 460, 310, 20, GxEPD_WHITE);
  display.setFont(&FreeMonoBold9pt7b);
  display.setTextColor(GxEPD_BLACK);
  display.setTextSize(0);
  display.setCursor(400, 479);
  display.print("RSSI ");
  display.print(rssi);
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
  if(uart_avail)
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

int draw_bmp(imagedata_t *im) {
  //int drawBMP( int x, int y, uint8_t* data, long image_size) {
  bmp_file_header_t *bmp_file_header = (bmp_file_header_t *)im->image;
  bmp_image_header_t *bmp_image_header = (bmp_image_header_t *)&(im->image)[14];
  if (bmp_file_header->signature != 0x4D42) { // "BM"
    SERIALPRINTF("unknown file type %d\r\n", bmp_file_header->signature);
    return (-1);
  }
  if (bmp_image_header->bits_per_pixel > 4) {
    SERIALPRINTLN("to much colors");
    return (-2);
  }
  byte bit_per_pixel = bmp_image_header->bits_per_pixel;
  if (bmp_image_header->compression_method != 0) {
    SERIALPRINTLN("compression not supported");
    return (-3);
  }
  SERIALPRINTLN("");

  SERIALPRINTF("imagesize: %d\r\n", bmp_file_header->file_size);
  SERIALPRINTF("offset: %d\r\n", bmp_file_header->image_offset);

  int width = bmp_image_header->image_width;
  int height = bmp_image_header->image_height;
  if (height < 0) {
    height = 0 - height;
  }
  SERIALPRINTF("%dx%d/%d at %d (%d)\r\n", width, height, bit_per_pixel, bmp_file_header->image_offset, bmp_file_header->file_size);
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

typedef struct my_private_struct
{
  int xoff, yoff; // corner offset
} PRIVATE;

void PNGDraw(PNGDRAW *pDraw) {
  if(!pDraw->y) {
    SERIALPRINTF("Width:%d Pitch:%d Type:%d BPP:%d\r\n", pDraw->iWidth, pDraw->iPitch,  pDraw->iPixelType, pDraw->iBpp);
  }
  // writeImage writes direct to hw and get overwritten by display.display()
  //display.writeImage(&(pDraw->pPixels[0]), 0, pDraw->y, pDraw->iWidth, 1, false);
  
  for(int x = 0; x < pDraw->iPitch; ++x) {
    for(int ib = 0; ib < 8; ++ib) {
      if(pDraw->pPixels[x] & (1 << ib)) {
        display.drawPixel(x*8+7-ib, pDraw->y, GxEPD_WHITE);
      } else {
        display.drawPixel(x*8+7-ib, pDraw->y, GxEPD_BLACK);
      }
    }
  }
}

void draw_png(imagedata_t *im) {
  SERIALPRINTLN("draw png");
  int ret = png.openRAM((uint8_t *)im->image, im->image_size, PNGDraw);
  if(ret== PNG_SUCCESS) {
    SERIALPRINTF("image specs: (%d x %d), %d bpp, pixel type: %d\n", png.getWidth(), png.getHeight(), png.getBpp(), png.getPixelType());
    display.setFullWindow();
    display.fillScreen(GxEPD_WHITE);
    png.decode((void *)NULL, 0);
  } else {
    SERIALPRINTLN("open failed");
  }
}

void draw_image(imagedata_t *im) {
  if(im->type == T_BMP) {
    draw_bmp(im);
  } else if(im->type == T_PNG) {
    draw_png(im);
    //display.refresh();
  }
}

void go_to_sleep(uint32_t seconds) {
  digitalWrite(RST, LOW);
  if(in_otau) {
    return;
  }
  if(seconds) {
    esp_sleep_enable_timer_wakeup(FactorSeconds * seconds);
  }
  if(!is_loading) {
    esp_deep_sleep_enable_gpio_wakeup(BIT(KEY) | BIT(USB_PWR), ESP_GPIO_WAKEUP_GPIO_HIGH);
  }
  esp_deep_sleep_disable_rom_logging();
  rtc_run_millis += millis();
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
  SERIALPRINTLN("");
  if(cmd.startsWith("r")) {
    ESP.restart();
  } else if(cmd.startsWith("ssid ")) {
    strcpy(wificonfig.ssid, cmd.substring(5).c_str());
  } else if(cmd.startsWith("pw ")) {
    strcpy(wificonfig.password, cmd.substring(3).c_str());
  } else if(cmd.startsWith("ipfix ")) {
    wificonfig.fake_fixed_ip = cmd.endsWith("y");
  } else if(cmd.startsWith("name ")) {
    strcpy(serverconfig.displayname, cmd.substring(5).c_str());
  } else if(cmd.startsWith("url ")) {
    strcpy(serverconfig.image_url, cmd.substring(4).c_str());
  } else if(cmd.startsWith("wake ")) {
    serverconfig.interval = cmd.substring(5).toInt();
  } else if(cmd.startsWith("uurl ")) {
    strcpy(serverconfig.update_url, cmd.substring(5).c_str());
  } else if(cmd.startsWith("umillis ")) {
    serverconfig.umillis = cmd.substring(8).toInt();
  } else if(cmd.startsWith("sh")) {
    SERIALPRINT("AP: ");
    SERIALPRINTLN(wificonfig.ssid);
    SERIALPRINT("PW: ");
    SERIALPRINTLN(wificonfig.password);
    SERIALPRINT("IPFIX: ");
    SERIALPRINTLN(wificonfig.fake_fixed_ip);
    SERIALPRINT("IP: ");
    SERIALPRINTLN(WiFi.localIP());
    SERIALPRINT("Name: ");
    SERIALPRINTLN(serverconfig.displayname);
    SERIALPRINT("URL: ");
    SERIALPRINTLN(serverconfig.image_url);
    SERIALPRINT("Wake Interval: ");
    SERIALPRINTLN(serverconfig.interval);
    SERIALPRINT("Update URL: ");
    SERIALPRINTLN(serverconfig.update_url);
    SERIALPRINT("Update RunMillis: ");
    SERIALPRINTLN(serverconfig.umillis);
  } else if(cmd.startsWith("sa")) {
    if(!preferences.begin("wificonfig")) {
      SERIALPRINTLN("unable to open Preferences for wifi config");
    } else {
      int x = preferences.putString("ssid", wificonfig.ssid);
      //int x = preferences.putBytes("ssid", wifi.ssid, strlen(wifi.ssid));
      SERIALPRINTF("stored ssid len %d\r\n", x);
      x = preferences.putString("password", wificonfig.password);
      SERIALPRINTF("stored passwd len %d\r\n", x);
      x = preferences.putBool("fixed_ip", wificonfig.fake_fixed_ip);
      SERIALPRINTF("stored IPFix len %d\r\n", x);
      preferences.end();
    }
    if(!preferences.begin("serverconfig")) {
      SERIALPRINTLN("unable to open Preferences for serverconfig");
    } else {
      int x = preferences.putString("displayname", serverconfig.displayname);
      SERIALPRINTF("stored name len %d\r\n", x);
      x = preferences.putString("image_url", serverconfig.image_url);
      SERIALPRINTF("stored url len %d\r\n", x);
      x = preferences.putString("update_url", serverconfig.update_url);
      SERIALPRINTF("stored update len %d\r\n", x);
      x = preferences.putLong("interval", serverconfig.interval);
      SERIALPRINTF("stored interval %d\r\n", x);
      x = preferences.putLong("umillis", serverconfig.umillis);
      SERIALPRINTF("stored umillis %d\r\n", x);
      preferences.end();
    }
  } else if(cmd.startsWith("h")) {
    SERIALPRINTLN("ssid <str>");
    SERIALPRINTLN("pw <str>");
    SERIALPRINTLN("name <str>");
    SERIALPRINTLN("url <uri>");
    SERIALPRINTLN("wake <int>");
    SERIALPRINTLN("uurl <uri>");
    SERIALPRINTLN("umillis <int>");
    SERIALPRINTLN("save");
    SERIALPRINTLN("restart");
    SERIALPRINTLN("");
  } else {
    SERIALPRINTLN(cmd);
    SERIALPRINTLN(" unknown (help?)");
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
        SERIALPRINT(c);
        SERIALPRINT(" ");
        SERIALPRINT(c);
      } else {
        SERIALPRINT(c);
        cmd += c;
      }
    }
    vTaskDelay(5);
	}
}

//try to get update from server, auto restart if done, else return
int get_update() {
  WiFiClient client;
  int ok = 0;
  SERIALPRINTLN("check for update");
  SERIALPRINTLN(serverconfig.update_url);
  if (!strlen(serverconfig.update_url)) {
    return(0);
  }
  t_httpUpdate_return ret = httpUpdate.update(client, serverconfig.update_url, VERSION);
  switch (ret) {
    case HTTP_UPDATE_FAILED: 
      SERIALPRINTF("HTTP_UPDATE_FAILD Error (%d): %s\n", httpUpdate.getLastError(), httpUpdate.getLastErrorString().c_str());
      break;

    case HTTP_UPDATE_NO_UPDATES:
      SERIALPRINTLN("HTTP_UPDATE_NO_UPDATES");
      ok = 1;
      break;

    case HTTP_UPDATE_OK:
      SERIALPRINTLN("HTTP_UPDATE_OK");
      ok = 1;
      break;
  }
  return(ok);
}

int fetch_image(struct imagedata_t *im) {
  SERIALPRINTLN("check for image");
  SERIALPRINTLN(serverconfig.image_url);
  int tries = 3;
  int got_response = 0;
  char image_time_str[32];
  char next_interval_str[32];
  char voltage[16];
  HTTPClient http;
  im->sleep = 60;
  im->type = UNKNOWN;
  while (tries) {
    tries--;
    http.begin(serverconfig.image_url);
    const char* headerNames[] = {"Date", "Content-Length", "Content-Type", "ContentHash", "Sleep", "NewVersion", "Version"};
    http.collectHeaders(headerNames, sizeof(headerNames) / sizeof(headerNames[0]));
    sprintf(voltage, "%d", bat_mV);
    // If-Modified-Since: Wed, 21 Oct 2015 07:28:00 GMT
    http.addHeader("If-Modified-Since", rtc_image_time);
    http.addHeader("DisplayName", serverconfig.displayname);
    http.addHeader("Version", VERSION);
    http.addHeader("ContentHash", rtc_contenthash);
    http.addHeader("RSSI", String(rssi));
    http.addHeader("WiFiFail", String(rtc_wifi_fail));
    http.addHeader("WiFiTime", String(wifi_connect_time));
    http.addHeader("RunMillis", String(rtc_run_millis));
    http.addHeader("BatteriePower", voltage);
    http.addHeader("Wakeup", wakeup_by);
    int httpCode = http.GET();
    if (httpCode > 0) {
      // HTTP header has been send and Server response header has been handled
      got_response = 1;
      SERIALPRINTF("[HTTP] GET... code: %d\r\n", httpCode);
      if (httpCode == 304) {
        http.end();
        return (2);
      }
      http.header("Date").toCharArray(rtc_image_time, sizeof(rtc_image_time));
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
        SERIALPRINTLN("load the image");
        WiFiClient w = http.getStream();
        int s = 0;
        int sz = 0;
        delay(10);
        im->image = (uint8_t*)malloc(content_len);
        uint8_t *imptr = im->image;
        //s = w.read(imptr, IMAGESIZE - s);
        s = w.readBytes(imptr, content_len - s);
        sz += s;
        SERIALPRINTF("read %d(%d of %d)\r\n", s, sz, content_len);
        imptr += s;
        int receive_tries = 15;
        while(http.connected() && ((s > 0) || receive_tries)) {
          if (s <= 0) {
            delay(50);
            receive_tries--;
            yield();
          }
          //s = w.read(imptr, sizeof(*im->image) - s);
          s = w.readBytes(imptr, content_len - s);
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
        SERIALPRINTF("read sum %d (%d)\r\n", sz, content_len);
        if (sz >= content_len) {
          http.header("ContentHash").toCharArray(im->contenthash, sizeof(im->contenthash));
          SERIALPRINT("Content-Type:");
          SERIALPRINTLN(http.header("Content-Type"));
          if(http.header("Content-Type").endsWith("bmp")) {
            im->type = T_BMP;
          } else if(http.header("Content-Type").endsWith("png")) {
            im->type = T_PNG;
          }
          http.end();
          return (1);
        }
      }
      SERIALPRINTF("[HTTP] GET... failed, error: %s\n", http.errorToString(httpCode).c_str());
      break;
    } else {
      SERIALPRINTLN("[HTTP] got no response");
    }
    http.end();
  }
  if(!got_response) {
    displayInfo(NoHttpResponse);
    rtc_contenthash[0] = '\0';
  }
  return (0);
}

void fetchAndDraw() {
  if(server_is_configured) {
    imagedata_t image;
    digitalWrite(D0, HIGH);
    int ret = fetch_image(&image);
    digitalWrite(D0, LOW);
    if(!IS_USB_CONNECTED) {
      WiFi.mode(WIFI_OFF);
    }
    if(ret == 1) {
      //digitalWrite(RST, HIGH);
      display.init();
      draw_image(&image);
      SERIALPRINTLN("draw done");
      draw_bat();
      if(IS_USB_CONNECTED) {
        draw_flash();
      }
      strcpy(rtc_contenthash, image.contenthash);
      SERIALPRINTLN("display::display");
      digitalWrite(D0, HIGH);
      display.display();
      SERIALPRINTLN("display::display done");
      if(!is_loading) {
        digitalWrite(RST, LOW);
      }
    } else if(ret == 2) {
      SERIALPRINTLN("no change");
    } else {
      //digitalWrite(RST, HIGH);
      display.init();
      displayInfo(NoHttpResponse);
    }
    if(!is_loading) {
      SERIALPRINTF("sleep %d seconds\r\n", image.sleep);
      go_to_sleep(image.sleep - millis()/1000);
    } else {
      SERIALPRINTF("no sleep %d seconds because usb connected\r\n", image.sleep);
      next_draw = millis() + image.sleep * 1000;
    }
  } else {
    //digitalWrite(RST, HIGH);
    display.init();
    displayInfo(NoServerConfig);
  }
}

void WiFiGotIP(WiFiEvent_t event, WiFiEventInfo_t info) {
  SERIALPRINTLN("WiFi connected");
  //SERIALPRINTF("WiFiIP: %lu\r\n", millis());
  SERIALPRINT("IP address: ");
  SERIALPRINTLN(IPAddress(info.got_ip.ip_info.ip.addr));
  SERIALPRINT("Hostname: ");
  SERIALPRINTLN(WiFi.getHostname());
  digitalWrite(D0, LOW);
  wifi_connect_time = millis() - wifi_start;
  wifi_wait = false;
  local_IP = info.got_ip.ip_info.ip.addr;
  subnet = info.got_ip.ip_info.netmask.addr;
  gateway = info.got_ip.ip_info.gw.addr;
  dns1 = WiFi.dnsIP(0);
  dns2 = WiFi.dnsIP(1);
  rssi = WiFi.RSSI();
  if(rssi < -80) {
    rtc_wifi_channel = 0; //bad connection, force full scan next time
  } else {
    rtc_wifi_channel = WiFi.channel();
  }
  if(serverconfig.umillis && ((rtc_run_millis - rtc_last_update_check) > serverconfig.umillis)) {
    rtc_last_update_check = rtc_run_millis;
    get_update();
  }
  fetchAndDraw();
}

void setup() {
  setCpuFrequencyMhz(80);
  digitalWrite(D0, HIGH);
  pinMode(D0, OUTPUT);
  pinMode(USB_PWR, INPUT);
  digitalWrite(RST, LOW);
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
    int q = Serial.availableForWrite(); // get serial queue length
    Serial.println(q);
    Serial.println("Initialization start"); //keep long to allow writalility detection
    delay(1); //allow serial to write
    if((q - Serial.availableForWrite()) < 20) { //at least some chars written out from queue
      Serial.println("uart ok");
      uart_avail = true;
    }
    is_loading = true;
  }
  esp_sleep_wakeup_cause_t wakeup_reason;
  wakeup_reason = esp_sleep_get_wakeup_cause();
  switch(wakeup_reason) {
    case ESP_SLEEP_WAKEUP_UNDEFINED:
      wakeup_by = "Reset";
      break;
    case ESP_SLEEP_WAKEUP_GPIO:
      wakeup_by = "GPIO";
      break;
    case ESP_SLEEP_WAKEUP_TIMER:
      wakeup_by = "Timer";
      break;
    default:
      wakeup_by = String(wakeup_reason);
      break;
  }
  digitalWrite(RST, HIGH);
  delayMicroseconds(10);
  analogReadResolution(12);
  analogSetAttenuation(ADC_11db);
  bat_mV = analogRead(BAT_VOLT) / 0.716; // 2973 / 4.15V
  digitalWrite(RST, LOW);
  if(digitalRead(KEY)) {
    delay(1000);
    if(digitalRead(KEY)) {
      rtc_contenthash[0] = '\0';
      local_IP = IPADDR_NONE;
      rtc_wifi_channel = 0;
    }
  }
  SERIALPRINTLN("init SPI");
  SPI.begin(CLK, DIN, DIN, CS); // map and init SPI pins SCK(13), MISO(12), MOSI(14), SS(15)
  SPI.setHwCs(true);
  //SPI.setFrequency(10000);
  if(!is_loading && bat_mV < 3300) {
    //digitalWrite(RST, HIGH);
    display.init();
    displayInfo(EmptyBat);
    go_to_sleep(0);
  }
  // check if we have wifi credentials
  wificonfig.ssid[0] = '\0';
  if(get_wlan_config(&wificonfig)) {
    WiFi.mode(WIFI_STA);
    if(0 && wificonfig.fake_fixed_ip && (local_IP != IPADDR_NONE)) {
      SERIALPRINTLN("using stored ip");
      WiFi.config(local_IP, gateway, subnet, dns1, dns2);
    }
    if(rtc_wifi_failed) {
      rtc_wifi_channel = 0;
    }
    if(!rtc_wifi_channel) {
      WiFi.setScanMethod(WIFI_ALL_CHANNEL_SCAN);
    }
    WiFi.setAutoReconnect(true);
    WiFi.begin(wificonfig.ssid, wificonfig.password, rtc_wifi_channel);
    //WiFi.setHostname(sys_config.hostname);
    //WiFi.onEvent(WiFiGotIP, WiFiEvent_t::SYSTEM_EVENT_STA_GOT_IP);
    WiFi.onEvent(WiFiGotIP, WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_GOT_IP);
    wifi_start = millis();
    wifi_wait = true;
  } else {
    wificonfig.fake_fixed_ip = true; // set to default
  }
  if (!get_server_config(&serverconfig)) {
    //digitalWrite(RST, HIGH);
    display.init();
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
        SERIALPRINTLN("Start updating " + type);
        in_otau = true;
      })
      .onEnd([]() {
        SERIALPRINTLN("\nEnd");
      })
      .onProgress([](unsigned int progress, unsigned int total) {
        SERIALPRINTF("Progress: %u%%\r", (progress / (total / 100)));
      })
      .onError([](ota_error_t error) {
        SERIALPRINTF("Error[%u]: ", error);
        if (error == OTA_AUTH_ERROR) SERIALPRINTLN("Auth Failed");
        else if (error == OTA_BEGIN_ERROR) SERIALPRINTLN("Begin Failed");
        else if (error == OTA_CONNECT_ERROR) SERIALPRINTLN("Connect Failed");
        else if (error == OTA_RECEIVE_ERROR) SERIALPRINTLN("Receive Failed");
        else if (error == OTA_END_ERROR) SERIALPRINTLN("End Failed");
        in_otau = false;
      });

    ArduinoOTA.begin();
    SERIALPRINTLN("OTAU Ready");
    xTaskCreate(update_loop, "update", 4096, NULL, 10, NULL);
  }

  if(IS_USB_CONNECTED) {
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
    rtc_wifi_fail++;
    rtc_wifi_failed = true;
    if(!is_loading) {
      go_to_sleep(10);
    }
  }
  if(next_draw && (ti > next_draw)) {
    fetchAndDraw();
  }
  if(digitalRead(KEY)) {
    ESP.restart();
  }
  if(is_loading && !IS_USB_CONNECTED) {
    ESP.restart();
  }
  if(!is_loading && (ti > (setup_fin + 600000))) {
    displayInfo(SleepForEver);
    go_to_sleep(0);
  }
  if(is_loading && serverconfig.umillis && ((ti - rtc_last_update_check) > serverconfig.umillis)) {
    rtc_last_update_check = ti;
    get_update();
  }
}