
#include <Arduino.h>
#include <lvgl.h>
#include "display.h"
#include "esp_bsp.h"
#include "lv_port.h"
#include <esp_log.h>
#include <WiFi.h>
#include <time.h>
#include <HTTPClient.h>

const char* ssid = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";
const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = 3600 * 9; // KST (UTC+9)
const int   daylightOffset_sec = 0;

static lv_obj_t *log_ta;
static lv_obj_t *ui_time_lbl; 
static lv_obj_t *weather_popup = NULL;
static lv_obj_t *weather_label = NULL;
static bool log_initialized = false;

// Weather Data
String city_name = "Seoul";
String temp_str = "--.- C";

// Fetch Weather simple function
void fetchWeather() {
    if (WiFi.status() == WL_CONNECTED) {
        HTTPClient http;
        // Seoul Coordinates
        http.begin("https://api.open-meteo.com/v1/forecast?latitude=37.5665&longitude=126.9780&current_weather=true");
        int httpCode = http.GET();
        if (httpCode > 0) {
            String payload = http.getString();
            int tempIndex = payload.indexOf("\"temperature\":");
            if (tempIndex != -1) {
                int end = payload.indexOf(",", tempIndex);
                String t = payload.substring(tempIndex + 14, end);
                temp_str = t + " C";
            }
        }
        http.end();
    }
}

// Log Queue Configuration
#define LOG_QUEUE_SIZE 30
#define LOG_MSG_MAX_LEN 128

typedef struct {
    char text[LOG_MSG_MAX_LEN];
} LogMessage;

static QueueHandle_t log_queue = NULL;

// Custom vprintf function to redirect ESP_LOG to Queue
extern "C" int vprintf_to_lvgl(const char *format, va_list args) {
    // 1. Format the message into a buffer once
    LogMessage msg;
    int len = vsnprintf(msg.text, sizeof(msg.text), format, args);
    
    if (len > 0) {
        // 2. Output to Serial (Avoid vprintf to prevent recursion!)
        // Print raw buffer to Serial (non-formatted print)
        Serial.print(msg.text);

        // 3. Send to Queue (Context safe)
        if (log_queue != NULL) {
            BaseType_t xHigherPriorityTaskWoken = pdFALSE;
            
            if (xPortInIsrContext()) {
                // ISR Context: Use FromISR API
                xQueueSendFromISR(log_queue, &msg, &xHigherPriorityTaskWoken);
                if (xHigherPriorityTaskWoken) portYIELD_FROM_ISR();
            } else {
                // Task Context
                xQueueSend(log_queue, &msg, 0);
            }
        }
    }
    return len;
}

// Helper: Process Logs from Queue to UI
void process_logs() {
    LogMessage msg;
    int process_count = 0;
    if (log_queue != NULL) {
        while (xQueueReceive(log_queue, &msg, 0) == pdTRUE) {
            if (bsp_display_lock(10)) {
                 lv_textarea_add_text(log_ta, msg.text);
                 // Memory protection
                 if (strlen(lv_textarea_get_text(log_ta)) > 8000) {
                    lv_textarea_set_text(log_ta, "--- CLEANED ---\n");
                 }
                 bsp_display_unlock();
            }
            process_count++;
            if (process_count > 10) break; // Don't block too long
        }
        // Force task yield to keep UI responsive
        if (process_count > 0) delay(1);
    }
}

void setup()
{
    // 1. USB Serial Init & Stability Delay
    Serial.begin(115200);
    
    // Create Log Queue
    log_queue = xQueueCreate(LOG_QUEUE_SIZE, sizeof(LogMessage));
    
    // Give 3 seconds for USB to stabilize
    delay(3000); 
    
    Serial.println("\n\n=== ESP32 Log Viewer Starting (Queue Mode) ===");

    // Initialize display & LVGL
    bsp_display_cfg_t cfg = {
        .lvgl_port_cfg = ESP_LVGL_PORT_INIT_CONFIG(),
        .buffer_size = EXAMPLE_LCD_QSPI_H_RES * EXAMPLE_LCD_QSPI_V_RES,
        .rotate = LV_DISP_ROT_90, // Landscape
    };

    bsp_display_start_with_config(&cfg);
    bsp_display_backlight_on();

    // Create Log Viewer UI
    bsp_display_lock(0);

    // Dark screen background
    lv_obj_set_style_bg_color(lv_scr_act(), lv_color_hex(0x000000), 0);

    // UI Header
    lv_obj_t * header = lv_obj_create(lv_scr_act());
    lv_obj_set_size(header, LV_PCT(100), 40);
    lv_obj_set_style_bg_color(header, lv_color_hex(0x1a1a1a), 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_style_border_side(header, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(header, lv_color_hex(0x00BFFF), 0);
    lv_obj_set_style_border_width(header, 2, 0);
    lv_obj_set_style_radius(header, 0, 0);
    lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 0);

    // Title label
    lv_obj_t * title = lv_label_create(header);
    lv_label_set_text(title, "ESP32-S3 REAL-TIME LOGS");
    lv_obj_set_style_text_color(title, lv_color_hex(0x00BFFF), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_align(title, LV_ALIGN_LEFT_MID, 10, 0);

    // Uptime label (Now Time Label)
    ui_time_lbl = lv_label_create(header);
    lv_label_set_text(ui_time_lbl, "CONNECTING...");
    lv_obj_set_style_text_color(ui_time_lbl, lv_color_hex(0x00FF00), 0);
    lv_obj_set_style_text_font(ui_time_lbl, &lv_font_montserrat_12, 0);
    lv_obj_align(ui_time_lbl, LV_ALIGN_RIGHT_MID, -10, 0);

    // Text Area for logs - fills the rest of the screen
    log_ta = lv_textarea_create(lv_scr_act());
    lv_obj_set_size(log_ta, LV_PCT(100), 280); // 320 (height) - 40 (header)
    lv_obj_align(log_ta, LV_ALIGN_BOTTOM_MID, 0, 0);
    
    // Style the text area - Terminal look
    lv_obj_set_style_bg_color(log_ta, lv_color_hex(0x050505), 0);
    lv_obj_set_style_text_color(log_ta, lv_color_hex(0x00FF00), 0);
    lv_obj_set_style_text_font(log_ta, &lv_font_montserrat_12, 0);
    lv_obj_set_style_border_width(log_ta, 0, 0);
    lv_obj_set_style_radius(log_ta, 0, 0);
    lv_obj_set_style_pad_all(log_ta, 5, 0);
    
    // Disable text area interaction (except scrolling)
    lv_textarea_set_cursor_click_pos(log_ta, false);
    
    lv_textarea_set_text(log_ta, ">>> SYSTEM INITIALIZED. QUEUE STARTED.\n");
    
    bsp_display_unlock();

    // Redirect logs
    log_initialized = true;
    esp_log_set_vprintf(vprintf_to_lvgl);
    // Ensure INFO-level logs are enabled at runtime
    esp_log_level_set("*", ESP_LOG_INFO);

    ESP_LOGI("BOOT", "Log Queue initialized with size %d", LOG_QUEUE_SIZE);

    // --- Wi-Fi Connection & Time Sync ---
    ESP_LOGI("WIFI", "Connecting to %s...", ssid);
    WiFi.begin(ssid, password);
    
    int retry = 0;
    while (WiFi.status() != WL_CONNECTED && retry < 40) { // Increased Retry
        delay(250); // Shorten delay
        ESP_LOGI("WIFI", ".");
        process_logs(); // CRITICAL: Update UI during blocking wait
        retry++;
    }

    if (WiFi.status() == WL_CONNECTED) {
        ESP_LOGI("WIFI", "Connected! IP: %s", WiFi.localIP().toString().c_str());
        
        // Init Time
        configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
        ESP_LOGI("TIME", "Syncing time with NTP...");
    } else {
        ESP_LOGE("WIFI", "Connection Failed! Check SSID/PW.");
        if (bsp_display_lock(10)) {
             lv_label_set_text(ui_time_lbl, "WIFI ERROR");
             bsp_display_unlock();
        }
    }
    process_logs(); // Final flush
}
 
void loop()
{
    // 1. Process Log Queue -> UI Update
    process_logs();

    // 2. Periodic Updates (Toggle System / Weather)
    static uint32_t last_toggle = 0;
    static uint32_t last_time_update = 0;
    static bool show_weather_mode = false; // Start with System Info
    static int cycle_counter = 0;
    
    // Time Update (Every 1 second)
    if (millis() - last_time_update > 1000) {
        struct tm timeinfo;
        if (getLocalTime(&timeinfo, 0)) { 
             if (bsp_display_lock(10)) {
                 char time_buf[64];
                 strftime(time_buf, sizeof(time_buf), "[%Y-%m-%d %H:%M:%S]", &timeinfo);
                 lv_label_set_text(ui_time_lbl, time_buf);
                 
                 // Update Weather Popup Time if visible
                 if (show_weather_mode && weather_label) {
                    char big_buf[128];
                    snprintf(big_buf, sizeof(big_buf), "%s\n%s\n%s", 
                             time_buf, city_name.c_str(), temp_str.c_str());
                    lv_label_set_text(weather_label, big_buf);
                 }
                 bsp_display_unlock();
             }
        }
        last_time_update = millis();
    }

    // Toggle Mode (Every 5 seconds)
    if (millis() - last_toggle > 5000) { 
        show_weather_mode = !show_weather_mode;
        
        if (bsp_display_lock(50)) {
            if (show_weather_mode) {
                // [MODE: WEATHER] Show Popup
                if (!weather_popup) {
                    weather_popup = lv_obj_create(lv_scr_act());
                    lv_obj_set_size(weather_popup, 260, 180);
                    lv_obj_center(weather_popup);
                    lv_obj_set_style_bg_color(weather_popup, lv_color_hex(0x000000), 0);
                    lv_obj_set_style_border_color(weather_popup, lv_color_hex(0x00FF00), 0);
                    lv_obj_set_style_border_width(weather_popup, 3, 0);
                    
                    weather_label = lv_label_create(weather_popup);
                    lv_obj_set_style_text_color(weather_label, lv_color_hex(0x00FF00), 0);
                    lv_obj_set_style_text_font(weather_label, &lv_font_montserrat_14, 0); 
                    lv_obj_set_style_transform_zoom(weather_label, 512, 0); // 2x Zoom
                    lv_obj_center(weather_label);
                    lv_label_set_text(weather_label, "LOADING...");
                }
                
                // Ensure popup is visible and on top
                lv_obj_clear_flag(weather_popup, LV_OBJ_FLAG_HIDDEN);
                lv_obj_move_foreground(weather_popup);
                
                fetchWeather(); 
                
            } else {
                // [MODE: SYSTEM] Hide Popup
                if (weather_popup) {
                    lv_obj_add_flag(weather_popup, LV_OBJ_FLAG_HIDDEN);
                }
                
                // Clear log and announce mode start
                ESP_LOGW("MODE", ">>> SYSTEM MONITORING STARTED <<<");
                lv_obj_scroll_to_y(log_ta, LV_COORD_MAX, LV_ANIM_OFF);
            }
            bsp_display_unlock();
        }
        last_toggle = millis();
    }
    
    // Continuous System Logging (Only when NOT in weather mode)
    static uint32_t last_detail_log = 0;
    if (!show_weather_mode && (millis() - last_detail_log > 500)) { 
        // Print detailed stats every 0.5 sec
        int8_t rssi = WiFi.RSSI();
        uint32_t free_heap = esp_get_free_heap_size();
        uint32_t min_heap = esp_get_minimum_free_heap_size();
        
        // logs with colors (using log tags)
        ESP_LOGI("CTX", "H:%6u | mH:%6u | WIFI:%3d", free_heap, min_heap, rssi);
        
        // Random "Traffic" simulation for valid hacker feel
        if (random(0, 100) > 70) {
             ESP_LOGI("NET", "Packet In: %d bytes from 192.168.0.%d", random(64, 1500), random(2, 254));
        }

        // Auto scroll
        if (bsp_display_lock(10)) {
            lv_obj_scroll_to_y(log_ta, LV_COORD_MAX, LV_ANIM_ON);
            bsp_display_unlock();
        }
        last_detail_log = millis();
    }
    
    delay(5); // Minimum delay for IDLE task
}

