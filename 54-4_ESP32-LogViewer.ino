
#ifndef CORE_DEBUG_LEVEL
#define CORE_DEBUG_LEVEL 5
#endif
#include <Arduino.h>
#include <lvgl.h>
#include "display.h"
#include "esp_bsp.h"
#include "lv_port.h"
#ifndef LOG_LOCAL_LEVEL
#define LOG_LOCAL_LEVEL ESP_LOG_VERBOSE
#endif
#include <esp_log.h>
#include <WiFi.h>
#include <time.h>
#include <HTTPClient.h>

const char* ssid     = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";
const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = 3600 * 9;
const int   daylightOffset_sec = 0;
const char* OWM_KEY = "YOUR_OWM_API_KEY";
const char* OWM_URL = "http://api.openweathermap.org/data/2.5/weather?lat=37.5665&lon=126.9780&units=metric&lang=en&appid=";

// Set to 1 to re-enable the old demo log spam after boot.
#ifndef ENABLE_DEMO_LOG_STREAM
#define ENABLE_DEMO_LOG_STREAM 0
#endif

// UI
static lv_obj_t *lbl_title   = NULL;
static lv_obj_t *lbl_time    = NULL;
static lv_obj_t *box_time    = NULL;
static lv_obj_t *lbl_uptime  = NULL;
static lv_obj_t *lbl_sys     = NULL;
static lv_obj_t *lbl_weather = NULL;
static lv_obj_t *lbl_log     = NULL;
static lv_obj_t *log_cont    = NULL;
static lv_obj_t *clock_meter = NULL;
static lv_meter_indicator_t *needle_hr = NULL;
static lv_meter_indicator_t *needle_min = NULL;
static lv_meter_indicator_t *needle_sec = NULL;

// --- Theme & Globals ---
static lv_meter_scale_t *scale = NULL;
// Loop timers (global for refresh trigger)
static uint32_t lLog=0, lSys=0, lTime=0, lWeather=0, lProd=0, lWR=0, lLv=0, lNet=0, lTask=0, lHook=0;
static bool is_dark = true;

typedef struct {
    lv_color_t bg;
    lv_color_t top_lbl;
    lv_color_t top_num;
    lv_color_t top_unit;
    lv_color_t bot_lbl;
    lv_color_t bot_num;
    lv_color_t log_txt;
    lv_color_t clock_border;
    lv_color_t clock_tick;
    lv_color_t clock_needle;
    lv_color_t title_txt;
} Theme_t;

static const Theme_t theme_dark = {
    .bg = lv_color_hex(0x000000),
    .top_lbl = lv_color_hex(0xB3B3B3), .top_num = lv_color_hex(0xFFFFFF), .top_unit = lv_color_hex(0x999999),
    .bot_lbl = lv_color_hex(0x00B300), .bot_num = lv_color_hex(0x00FF00),
    .log_txt = lv_color_hex(0x00FF00),
    .clock_border = lv_color_hex(0x008000), .clock_tick = lv_color_hex(0x008000), .clock_needle = lv_color_hex(0x00FF00),
    .title_txt = lv_color_hex(0x00BFFF)
};

static const Theme_t theme_light = {
    .bg = lv_color_hex(0xE0E0E0),
    .top_lbl = lv_color_hex(0x333333), .top_num = lv_color_hex(0x000000), .top_unit = lv_color_hex(0x555555),
    .bot_lbl = lv_color_hex(0x005500), .bot_num = lv_color_hex(0x00AA00),
    .log_txt = lv_color_hex(0x000000),
    .clock_border = lv_color_hex(0x444444), .clock_tick = lv_color_hex(0x444444), .clock_needle = lv_color_hex(0xFF0000),
    .title_txt = lv_color_hex(0x0055FF)
};

static const Theme_t *curr_theme = &theme_dark;

// Macros for compatibility with existing loop code
#define COLOR_TOP_LABEL (curr_theme->top_lbl)
#define COLOR_TOP_NUM   (curr_theme->top_num)
#define COLOR_TOP_UNIT  (curr_theme->top_unit)
#define COLOR_BOT_LABEL (curr_theme->bot_lbl)
#define COLOR_BOT_NUM   (curr_theme->bot_num)

// Helper to create/recreate clock
void create_clock_meter() {
    if(clock_meter) {
        lv_obj_del(clock_meter);
        clock_meter = NULL;
    }
    
    clock_meter = lv_meter_create(lv_scr_act());
    lv_obj_set_size(clock_meter, 115, 115);
    lv_obj_set_pos(clock_meter, 356, 110);
    lv_obj_set_style_bg_opa(clock_meter, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(clock_meter, 1, 0);
    lv_obj_set_style_border_color(clock_meter, curr_theme->clock_border, 0);
    lv_obj_set_style_text_opa(clock_meter, LV_OPA_0, LV_PART_TICKS); // Hide numbers

    scale = lv_meter_add_scale(clock_meter);
    lv_meter_set_scale_ticks(clock_meter, scale, 61, 0, 0, curr_theme->clock_tick);
    lv_meter_set_scale_major_ticks(clock_meter, scale, 15, 4, 15, curr_theme->clock_tick, 10);
    lv_meter_set_scale_range(clock_meter, scale, 0, 600, 360, 270);

    lv_obj_add_flag(clock_meter, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(clock_meter, screen_touch_cb, LV_EVENT_CLICKED, NULL);

    needle_hr = lv_meter_add_needle_line(clock_meter, scale, 3, curr_theme->clock_needle, -10);
    needle_min = lv_meter_add_needle_line(clock_meter, scale, 2, curr_theme->clock_needle, -5);
    needle_sec = lv_meter_add_needle_line(clock_meter, scale, 1, curr_theme->clock_needle, 0);
}

void update_theme() {
    curr_theme = is_dark ? &theme_dark : &theme_light;
    lv_obj_set_style_bg_color(lv_scr_act(), curr_theme->bg, 0);
    if(lbl_title) lv_obj_set_style_text_color(lbl_title, curr_theme->title_txt, 0);
    if(lbl_time) lv_obj_set_style_text_color(lbl_time, curr_theme->top_num, 0);
    if(lbl_log) lv_obj_set_style_text_color(lbl_log, curr_theme->log_txt, 0);
    
    // Recreate clock with new colors
    create_clock_meter();
    
    lSys = 0; lWeather = 0;
    lv_obj_invalidate(lv_scr_act());
}

static void screen_touch_cb(lv_event_t * e) {
    ESP_LOGI("TOUCH", "Screen Clicked");
    is_dark = !is_dark;
    update_theme();
}

static void spangroup_clear(lv_obj_t *sg) {
    if (!sg) return;
    uint32_t cnt = lv_spangroup_get_child_cnt(sg);
    for (uint32_t i = 0; i < cnt; i++) {
        lv_span_t *sp = lv_spangroup_get_child(sg, 0);
        if (sp) lv_spangroup_del_span(sg, sp);
    }
}

static void span_set_style(lv_span_t *sp, const lv_font_t *font, lv_color_t color) {
    if (!sp) return;
    lv_style_init(&sp->style);
    lv_style_set_text_font(&sp->style, font);
    lv_style_set_text_color(&sp->style, color);
}

static void format_uptime(char *out, size_t out_sz, uint32_t seconds) {
    if (out_sz == 0) return;
    if (seconds < 60) {
        snprintf(out, out_sz, "%lus", (unsigned long)seconds);
    } else {
        uint32_t h = seconds / 3600;
        uint32_t m = (seconds % 3600) / 60;
        uint32_t s = seconds % 60;
        snprintf(out, out_sz, "%lu:%02lu:%02lu",
            (unsigned long)h, (unsigned long)m, (unsigned long)s);
    }
}

// Display log buffer - keep small to avoid LVGL rendering stack overflow
#define DISP_MAX 20000
static char disp_buf[DISP_MAX + 1];
static int  disp_len = 0;

static void format_u32_commas(uint32_t v, char *out, size_t out_sz) {
    char buf[16];
    snprintf(buf, sizeof(buf), "%u", (unsigned)v);
    int len = (int)strlen(buf);
    int commas = (len - 1) / 3;
    int out_len = len + commas;
    if (out_sz == 0) return;
    if ((int)out_sz <= out_len) {
        // Fallback: truncate without commas if buffer is too small
        snprintf(out, out_sz, "%s", buf);
        return;
    }
    out[out_len] = '\0';
    int i = len - 1;
    int j = out_len - 1;
    int group = 0;
    while (i >= 0) {
        out[j--] = buf[i--];
        if (++group == 3 && i >= 0) {
            out[j--] = ',';
            group = 0;
        }
    }
}

static void format_i32_commas(int32_t v, char *out, size_t out_sz) {
    if (v < 0) {
        if (out_sz < 2) return;
        out[0] = '-';
        format_u32_commas((uint32_t)(-v), out + 1, out_sz - 1);
        return;
    }
    format_u32_commas((uint32_t)v, out, out_sz);
}

void disp_append(const char *text) {
    int tlen = strlen(text);
    if (tlen <= 0) return;
    // If overflow, drop oldest half
    if (disp_len + tlen >= DISP_MAX) {
        int cut = disp_len / 2;
        const char *nl = strchr(disp_buf + cut, '\n');
        if (nl) cut = (int)(nl - disp_buf) + 1;
        if (cut > 0 && cut < disp_len) {
            memmove(disp_buf, disp_buf + cut, disp_len - cut);
            disp_len -= cut;
        } else {
            disp_len = 0;
        }
        disp_buf[disp_len] = '\0';
    }
    // Truncate if single message too long
    if (tlen > DISP_MAX - disp_len - 1) tlen = DISP_MAX - disp_len - 1;
    memcpy(disp_buf + disp_len, text, tlen);
    disp_len += tlen;
    disp_buf[disp_len] = '\0';
}

// Get visible tail (last ~9000 chars, line-aligned)
const char* disp_tail() {
    if (disp_len < 9000) return disp_buf;
    const char *p = disp_buf + disp_len - 9000;
    const char *nl = strchr(p, '\n');
    return nl ? nl + 1 : p;
}

// Weather data
static char w_temp[16]="--", w_feel[16]="--", w_desc[32]="---";
static char w_humi[8]="--", w_wind[16]="--", w_city[16]="Seoul";
static volatile bool weather_fetching = false;

// Log queue
#define LOG_QUEUE_SIZE  80
#define LOG_MSG_LEN     120
typedef struct { char text[LOG_MSG_LEN]; } LogMessage;
static QueueHandle_t log_queue = NULL;

extern "C" int vprintf_to_lvgl(const char *format, va_list args) {
    LogMessage msg;
    int len = vsnprintf(msg.text, sizeof(msg.text), format, args);
    if (len > 0) {
        // Normalize CR to LF so the label doesn't keep overwriting the same line.
        for (int i = 0; i < len && i < (int)sizeof(msg.text); i++) {
            if (msg.text[i] == '\r') msg.text[i] = '\n';
        }
        // Ensure each log line ends with newline for proper scrolling
        int max_len = (int)sizeof(msg.text) - 1;
        if (len > 0 && len < max_len && msg.text[len - 1] != '\n') {
            msg.text[len++] = '\n';
            msg.text[len] = '\0';
        }
        Serial.print(msg.text);
        if (log_queue) xQueueSend(log_queue, &msg, 0);
    }
    return len;
}

String jsonVal(const String &j, const char *key) {
    String s = String("\"") + key + "\":";
    int i = j.indexOf(s); if (i<0) return "";
    i += s.length();
    if (j[i]=='"') { int e=j.indexOf('"',i+1); return j.substring(i+1,e); }
    int e=i; while(e<(int)j.length()&&j[e]!=','&&j[e]!='}') e++;
    return j.substring(i,e);
}

void weatherTask(void *p) {
    if (WiFi.status()!=WL_CONNECTED){weather_fetching=false;vTaskDelete(NULL);return;}
    HTTPClient http;
    http.begin(String(OWM_URL)+OWM_KEY); http.setTimeout(6000);
    int code=http.GET();
    if(code==HTTP_CODE_OK){
        String r=http.getString();
        String t=jsonVal(r,"temp"),f=jsonVal(r,"feels_like"),h=jsonVal(r,"humidity");
        String d=jsonVal(r,"description"),ws=jsonVal(r,"speed"),n=jsonVal(r,"name");
        if(t.length()) strncpy(w_temp,t.c_str(),15);
        if(f.length()) strncpy(w_feel,f.c_str(),15);
        if(h.length()) strncpy(w_humi,h.c_str(),7);
        if(d.length()) strncpy(w_desc,d.c_str(),31);
        if(ws.length())strncpy(w_wind,ws.c_str(),15);
        if(n.length()) strncpy(w_city,n.c_str(),15);
        ESP_LOGI("OWM","%s %sC %s",w_city,w_temp,w_desc);
    } else { ESP_LOGE("OWM","HTTP%d",code); }
    http.end(); weather_fetching=false; vTaskDelete(NULL);
}
void fetchWeatherAsync(){
    if(weather_fetching)return; weather_fetching=true;
    xTaskCreatePinnedToCore(weatherTask,"owm",8192,NULL,2,NULL,0);
}

void setup() {
    Serial.begin(115200);
    log_queue = xQueueCreate(LOG_QUEUE_SIZE, sizeof(LogMessage));
    memset(disp_buf, 0, sizeof(disp_buf));
    strcpy(disp_buf, "> BOOT\n"); disp_len = 7;
    delay(3000);

    // Standard config (task_stack=4096 default)
    bsp_display_cfg_t cfg = {
        .lvgl_port_cfg = ESP_LVGL_PORT_INIT_CONFIG(),
        .buffer_size = EXAMPLE_LCD_QSPI_H_RES * EXAMPLE_LCD_QSPI_V_RES,
        .rotate = LV_DISP_ROT_90,
    };
    bsp_display_start_with_config(&cfg);
    bsp_display_backlight_on();

    // BUILD UI (480x320 landscape)
    bsp_display_lock(0);
    lv_obj_set_style_bg_color(lv_scr_act(), curr_theme->bg, 0);

    // Row1: title + time
    lbl_title = lv_label_create(lv_scr_act());
    lv_label_set_text(lbl_title, "ESP32-S3 MONITOR");
    lv_obj_set_style_text_color(lbl_title, curr_theme->title_txt, 0);
    lv_obj_set_style_text_font(lbl_title, &lv_font_montserrat_20, 0);
    lv_obj_set_pos(lbl_title, 8, 2);

    // Uptime next to title (top row)
    lbl_uptime = lv_spangroup_create(lv_scr_act());
    lv_obj_set_style_text_color(lbl_uptime, COLOR_TOP_LABEL, 0);
    lv_obj_set_style_text_font(lbl_uptime, &lv_font_montserrat_16, 0);
    lv_obj_set_pos(lbl_uptime, 250, 6);
    lv_obj_set_size(lbl_uptime, 120, 20);
    lv_spangroup_set_align(lbl_uptime, LV_TEXT_ALIGN_LEFT);

    // Fixed-width time box (2 lines) above analog clock
    box_time = lv_obj_create(lv_scr_act());
    lv_obj_set_size(box_time, 120, 44);
    lv_obj_set_style_bg_opa(box_time, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(box_time, 0, 0);
    lv_obj_set_style_pad_all(box_time, 0, 0);
    lv_obj_set_pos(box_time, 356, 8);

    lbl_time = lv_label_create(box_time);
    lv_label_set_text(lbl_time, "----.--.--\n--:--:--");
    lv_obj_set_style_text_color(lbl_time, curr_theme->top_num, 0);
    lv_obj_set_style_text_font(lbl_time, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_align(lbl_time, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_size(lbl_time, 120, 44);
    lv_label_set_long_mode(lbl_time, LV_LABEL_LONG_CLIP);
    lv_obj_align(lbl_time, LV_ALIGN_RIGHT_MID, 0, 0);

    // Row2: system
    lbl_sys = lv_spangroup_create(lv_scr_act());
    lv_obj_set_style_text_color(lbl_sys, COLOR_TOP_LABEL, 0);
    lv_obj_set_style_text_font(lbl_sys, &lv_font_montserrat_16, 0);
    lv_obj_set_pos(lbl_sys, 8, 30);
    lv_obj_set_size(lbl_sys, 472, 34);
    lv_spangroup_set_align(lbl_sys, LV_TEXT_ALIGN_LEFT);

    // Row3: weather
    lbl_weather = lv_spangroup_create(lv_scr_act());
    lv_obj_set_style_text_color(lbl_weather, COLOR_BOT_LABEL, 0);
    lv_obj_set_style_text_font(lbl_weather, &lv_font_montserrat_12, 0);
    lv_obj_set_pos(lbl_weather, 356, 241);
    lv_obj_set_size(lbl_weather, 115, 70);
    lv_spangroup_set_align(lbl_weather, LV_TEXT_ALIGN_LEFT);

    // Log container (scrollable) + label
    log_cont = lv_obj_create(lv_scr_act());
    lv_obj_set_pos(log_cont, 4, 92);
    lv_obj_set_size(log_cont, 362, 228);
    lv_obj_set_style_bg_opa(log_cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(log_cont, 0, 0);
    lv_obj_set_style_pad_all(log_cont, 0, 0);
    lv_obj_set_scrollbar_mode(log_cont, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_scroll_dir(log_cont, LV_DIR_VER);

    lbl_log = lv_label_create(log_cont);
    lv_obj_set_width(lbl_log, 362);
    lv_label_set_long_mode(lbl_log, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(lbl_log, curr_theme->log_txt, 0);
    lv_obj_set_style_text_font(lbl_log, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_line_space(lbl_log, 1, 0);
    lv_label_set_text(lbl_log, disp_buf);

    // Initial clock create
    create_clock_meter();

    bsp_display_unlock();

    // Redirect logs
    esp_log_set_vprintf(vprintf_to_lvgl);
    esp_log_level_set("*", ESP_LOG_VERBOSE);
    esp_log_level_set("wifi", ESP_LOG_VERBOSE);
    esp_log_level_set("wifi_init", ESP_LOG_VERBOSE);
    esp_log_level_set("tcpip_adapter", ESP_LOG_VERBOSE);
    esp_log_level_set("IP", ESP_LOG_VERBOSE);
    esp_log_level_set("WIFI", ESP_LOG_VERBOSE);
    Serial.printf("LOG_LOCAL_LEVEL=%d CORE_DEBUG_LEVEL=%d\n", LOG_LOCAL_LEVEL, CORE_DEBUG_LEVEL);
    ESP_LOGI("BOOT", "Display ready");

    // Wi-Fi
    ESP_LOGI("WIFI", "Connecting %s", ssid);
    WiFi.begin(ssid, password);
    int r=0;
    while(WiFi.status()!=WL_CONNECTED && r<40){delay(250);ESP_LOGI("WIFI",".");r++;}
    if (WiFi.status()==WL_CONNECTED) {
        ESP_LOGI("WIFI","OK IP:%s",WiFi.localIP().toString().c_str());
        configTime(gmtOffset_sec,daylightOffset_sec,ntpServer);
        ESP_LOGI("NTP","Sync started");
        fetchWeatherAsync();
    } else { ESP_LOGE("WIFI","FAILED"); }
    ESP_LOGI("BOOT","Setup done");
}

void loop() {
    static uint32_t pkt=0;
    uint32_t now = millis();

    // Safety: drive LVGL tick handler from loop in case the LVGL task is stalled
    if (now - lLv >= 5) {
        if (bsp_display_lock(10)) {
            lv_timer_handler();
            bsp_display_unlock();
        }
        lLv = now;
    }

    // === 1. Drain queue into disp_buf (NO lock needed) ===
    bool changed = false;
    LogMessage msg;
    int cnt = 0;
    while (cnt < 20 && xQueueReceive(log_queue, &msg, 0) == pdTRUE) {
        disp_append(msg.text);
        changed = true;
        cnt++;
    }

    // === 2. Update display (every 150ms, short lock) ===
    if (now - lLog >= 150 && changed) {
        if (bsp_display_lock(200)) {
            lv_label_set_text(lbl_log, disp_tail());
            lv_obj_scroll_to_y(log_cont, LV_COORD_MAX, LV_ANIM_OFF);
            bsp_display_unlock();
        }
        lLog = now;
    }

    // === 3. Clock update (1s) ===
    if (now - lTime >= 1000) {
        struct tm ti;
        if (getLocalTime(&ti, 0)) {
            char b[32]; strftime(b, sizeof(b), "%Y-%m-%d\n%H:%M:%S", &ti);
            if (bsp_display_lock(200)) {
                lv_label_set_text(lbl_time, b);
                // Update analog clock
                if (clock_meter && needle_hr && needle_min && needle_sec) {
                    int hour = ti.tm_hour % 12;
                    int minute = ti.tm_min;
                    int second = ti.tm_sec;
                    
                    // Range 0-600
                    int hour_pos = (hour * 50) + (minute * 50) / 60;
                    int min_pos = minute * 10;
                    int sec_pos = second * 10;

                    lv_meter_set_indicator_value(clock_meter, needle_hr, hour_pos);
                    lv_meter_set_indicator_value(clock_meter, needle_min, min_pos);
                    lv_meter_set_indicator_value(clock_meter, needle_sec, sec_pos);
                }
                bsp_display_unlock();
            }
        }
        lTime = now;
    }

    // === 4. System info (500ms) ===
    if (now - lSys >= 500) {
        uint32_t fh=esp_get_free_heap_size(), mh=esp_get_minimum_free_heap_size();
        uint32_t pi=heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
        uint32_t pp=heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
        int8_t rssi=WiFi.RSSI();
        char fh_s[16], mh_s[16], pi_s[16], pp_s[16], rssi_s[8], up_s[16];
        format_u32_commas(fh, fh_s, sizeof(fh_s));
        format_u32_commas(mh, mh_s, sizeof(mh_s));
        format_u32_commas(pi, pi_s, sizeof(pi_s));
        format_u32_commas(pp, pp_s, sizeof(pp_s));
        format_i32_commas((int32_t)rssi, rssi_s, sizeof(rssi_s));
        format_u32_commas((uint32_t)(now/1000), up_s, sizeof(up_s));
        if (bsp_display_lock(200)) {
            spangroup_clear(lbl_sys);

            String heap_label = "Heap:";
            String heap_val = String(fh_s);
            String heap_unit = "B ";
            String min_label = "MinHeap:";
            String min_val = String(mh_s);
            String min_unit = "B";
            String int_label = "\nINTERNAL:";
            String int_val = String(pi_s);
            String int_unit = "B  ";
            String ps_label = "PSRAM:";
            String ps_val = String(pp_s);
            String ps_unit = "B  ";
            String rssi_label = "RSSI:";
            String rssi_val = String(rssi_s) + "dBm";

            auto add_span = [&](const String &txt, const lv_font_t *font, lv_color_t color) {
                lv_span_t *sp = lv_spangroup_new_span(lbl_sys);
                span_set_style(sp, font, color);
                lv_span_set_text(sp, txt.c_str());
            };

            add_span(heap_label, &lv_font_montserrat_12, COLOR_TOP_LABEL);
            add_span(heap_val, &lv_font_montserrat_16, COLOR_TOP_NUM);
            add_span(heap_unit, &lv_font_montserrat_12, COLOR_TOP_UNIT);
            add_span(min_label, &lv_font_montserrat_12, COLOR_TOP_LABEL);
            add_span(min_val, &lv_font_montserrat_16, COLOR_TOP_NUM);
            add_span(min_unit, &lv_font_montserrat_12, COLOR_TOP_UNIT);
            add_span(int_label, &lv_font_montserrat_12, COLOR_TOP_LABEL);
            add_span(int_val, &lv_font_montserrat_16, COLOR_TOP_NUM);
            add_span(int_unit, &lv_font_montserrat_12, COLOR_TOP_UNIT);
            add_span(ps_label, &lv_font_montserrat_12, COLOR_TOP_LABEL);
            add_span(ps_val, &lv_font_montserrat_16, COLOR_TOP_NUM);
            add_span(ps_unit, &lv_font_montserrat_12, COLOR_TOP_UNIT);
            add_span(rssi_label, &lv_font_montserrat_12, COLOR_TOP_LABEL);
            add_span(rssi_val, &lv_font_montserrat_16, COLOR_TOP_NUM);

            spangroup_clear(lbl_uptime);
            String up_label = "Uptime:";
            char up_buf[16];
            format_uptime(up_buf, sizeof(up_buf), (uint32_t)(now / 1000));
            String up_val = String(up_buf);
            lv_span_t *u1 = lv_spangroup_new_span(lbl_uptime);
            span_set_style(u1, &lv_font_montserrat_12, COLOR_TOP_LABEL);
            lv_span_set_text(u1, up_label.c_str());
            lv_span_t *u2 = lv_spangroup_new_span(lbl_uptime);
            span_set_style(u2, &lv_font_montserrat_16, COLOR_TOP_NUM);
            lv_span_set_text(u2, up_val.c_str());

            bsp_display_unlock();
        }
        lSys = now;
    }

    // === 5. Weather label (2s) ===
    if (now - lWeather >= 2000) {
        if (bsp_display_lock(200)) {
            spangroup_clear(lbl_weather);

            String city = String(w_city) + ", KR ";
            String temp = String(w_temp) + " C\n";
            String feel_label = "feel:";
            String feel_val = String(w_feel) + " ";
            String hum_label = "hum:";
            String hum_val = String(w_humi) + "%\n";
            String wind_label = "wind:";
            String wind_val = String(w_wind) + "m/s";

            lv_span_t *s1 = lv_spangroup_new_span(lbl_weather);
            span_set_style(s1, &lv_font_montserrat_12, COLOR_BOT_LABEL);
            lv_span_set_text(s1, city.c_str());

            lv_span_t *s2 = lv_spangroup_new_span(lbl_weather);
            span_set_style(s2, &lv_font_montserrat_12, COLOR_BOT_NUM);
            lv_span_set_text(s2, temp.c_str());

            lv_span_t *s3 = lv_spangroup_new_span(lbl_weather);
            span_set_style(s3, &lv_font_montserrat_12, COLOR_BOT_LABEL);
            lv_span_set_text(s3, feel_label.c_str());

            lv_span_t *s4 = lv_spangroup_new_span(lbl_weather);
            span_set_style(s4, &lv_font_montserrat_12, COLOR_BOT_NUM);
            lv_span_set_text(s4, feel_val.c_str());

            lv_span_t *s5 = lv_spangroup_new_span(lbl_weather);
            span_set_style(s5, &lv_font_montserrat_12, COLOR_BOT_LABEL);
            lv_span_set_text(s5, hum_label.c_str());

            lv_span_t *s6 = lv_spangroup_new_span(lbl_weather);
            span_set_style(s6, &lv_font_montserrat_12, COLOR_BOT_NUM);
            lv_span_set_text(s6, hum_val.c_str());

            lv_span_t *s7 = lv_spangroup_new_span(lbl_weather);
            span_set_style(s7, &lv_font_montserrat_12, COLOR_BOT_LABEL);
            lv_span_set_text(s7, wind_label.c_str());

            lv_span_t *s8 = lv_spangroup_new_span(lbl_weather);
            span_set_style(s8, &lv_font_montserrat_12, COLOR_BOT_NUM);
            lv_span_set_text(s8, wind_val.c_str());

            bsp_display_unlock();
        }
        lWeather = now;
    }

    // === 6. Produce logs (demo spam, optional) ===
#if ENABLE_DEMO_LOG_STREAM
    if (now - lProd >= 300) {
        uint32_t fh=esp_get_free_heap_size(), mh=esp_get_minimum_free_heap_size();
        uint32_t pi=heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
        uint32_t ps=heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
        int8_t rssi=WiFi.RSSI();

        esp_log_write(ESP_LOG_INFO, "MEM", "heap:%u min:%u int:%u ps:%u",fh,mh,pi,ps);
        esp_log_write(ESP_LOG_INFO, "NET", "rssi:%d up:%lus",rssi,(unsigned long)(now/1000));
        if(random(100)>50){pkt++;esp_log_write(ESP_LOG_INFO, "PKT", "#%u IN %dB 192.168.0.%d:%d",(unsigned)pkt,random(64,1500),random(2,254),random(1024,65535));}
        if(random(100)>70){pkt++;esp_log_write(ESP_LOG_INFO, "PKT", "#%u OUT %dB 10.0.0.%d:%d",(unsigned)pkt,random(64,1500),random(1,50),random(80,8080));}
        if(random(100)>85){esp_log_write(ESP_LOG_WARN, "CPU", "temp:%.1fC tasks:%d",temperatureRead(),uxTaskGetNumberOfTasks());}
        lProd=now;
    }
#endif

    // === 7. Weather fetch (60s) ===
    if(now-lWR>=60000){fetchWeatherAsync();lWR=now;}

    // === 8.5 Ensure log hook stays active (5s) ===
    if (now - lHook >= 5000) {
        esp_log_set_vprintf(vprintf_to_lvgl);
        lHook = now;
    }

    // === 9. Extra system logs (1s/2s/5s) ===
    if (now - lNet >= 1000) {
        esp_log_write(ESP_LOG_INFO, "MEM", "heap:%u min:%u int:%u ps:%u",
            (unsigned)esp_get_free_heap_size(),
            (unsigned)esp_get_minimum_free_heap_size(),
            (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
            (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
        esp_log_write(ESP_LOG_INFO, "NET", "wifi:%d rssi:%d ip:%s",
            (int)WiFi.status(),
            (int)WiFi.RSSI(),
            WiFi.localIP().toString().c_str());
        lNet = now;
    }

    if (now - lTask >= 2000) {
        esp_log_write(ESP_LOG_INFO, "RTOS", "tasks:%d tick:%u",
            (int)uxTaskGetNumberOfTasks(),
            (unsigned)xTaskGetTickCount());
        lTask = now;
    }


    delay(10);
}
