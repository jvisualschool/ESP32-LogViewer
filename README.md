# 📟 ESP32 Log Viewer (Real-Time System Monitor)

ESP32-S3의 모든 시스템 로그(ESP_LOG) 및 시리얼 출력을 3.5인치 QSPI LCD 화면에 **실시간 터미널** 형태로 보여주는 고성능 모니터링 프로젝트입니다.
`Serial.print` 뿐만 아니라 ESP-IDF 내부의 운영체제(FreeRTOS) 로그, Wi-Fi 상태, 메모리 할당 내역까지 모두 화면에 출력됩니다.

![Screenshot](screenshot.png)

## ✨ 주요 기능 (Key Features)

### 1. 🖥️ 실시간 로그 리다이렉션 (Real-time Log Redirection)

- **ESP_LOG Hook**: `esp_log_set_vprintf` API를 활용하여 ESP32의 표준 입출력을 가로채 LCD로 전달합니다.
- **Terminal UI**: 해커 스타일의 **Dark Mode (0x000000)** 배경과 **Matrix Green (0x00FF00)** 텍스트.
- **Auto Scroll**: 새로운 로그가 도착하면 자동으로 화면 최하단으로 스크롤되며, 링 버퍼를 통해 메모리를 효율적으로 관리합니다.

### 2. 📊 시스템 모니터링 (System Stats)

- **Memory Analysis**:
  - `Heap`: 실시간 가용 힙 메모리 (SRAM)
  - `MinHeap`: 부팅 후 최저 가용 메모리 (메모리 누수 확인용)
  - `Internal / PSRAM`: 내부 SRAM과 외부 PSRAM 사용량을 분리하여 추적
- **Network Status**:
  - `RSSI`: Wi-Fi 신호 강도 (dBm) 실시간 표시
  - `Uptime`: 시스템 가동 시간 (시:분:초)

### 3. ☁️ 날씨 및 시간 정보 (Environment Info)

- **NTP Time Sync**: `pool.ntp.org`를 통해 인터넷 시간을 받아와 정확한 시각을 표시합니다.
- **Analog & Digital Clock**: LVGL Meter 위젯을 활용한 아날로그 시계와 디지털 시계 동시 제공.
- **OpenWeatherMap Integration**: 서울(Seoul) 기준 실시간 기온, 체감온도, 습도, 풍속 정보를 API로 받아와 표시합니다.

### 4. 🛡️ 안정성 설계 (Robustness)

- **Thread Safety**: LVGL 렌더링 루프와 백그라운드 데이터 작업 간의 충돌 방지를 위해 Mutex (`bsp_display_lock`) 적용.
- **Memory Safety**: 로그 버퍼가 가득 차면 오래된 로그부터 자동 삭제하여 오버플로우 방지.

---

## 🛠️ 하드웨어 구성 (Hardware Spec)

이 프로젝트는 **ESP32-S3**의 고속 **QSPI (Quad SPI)** 인터페이스를 사용하여 480x320 해상도를 부드럽게 구동합니다.

| Component      | Specification | Description                             |
| -------------- | ------------- | --------------------------------------- |
| **MCU**        | ESP32-S3      | Dual-core, WiFi/BLE, AI Instruction     |
| **Flash**      | 16MB          | Large storage for app code & assets     |
| **PSRAM**      | 8MB (OPI)     | Octal SPI RAM for LVGL buffer & logging |
| **Display**    | 3.5" IPS LCD  | 480x320 Resolution                      |
| **Controller** | AXS15231B     | QSPI Interface Supported                |
| **Touch**      | GT911         | I2C Capacitive Touch (예정)             |

---

## ⚙️ 설정 방법 (Configuration)

`54-4_ESP32-LogViewer.ino` 파일 상단의 설정을 본인의 환경에 맞게 수정하세요.

```cpp
// Wi-Fi 설정
const char* ssid     = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";

// 날씨 API 설정 (OpenWeatherMap)
const char* OWM_KEY = "YOUR_API_KEY"; // https://openweathermap.org/api

// 시간대 설정 (한국: UTC+9 = 3600 * 9)
const long  gmtOffset_sec = 3600 * 9;
```

---

## 🚀 빌드 및 업로드 (Build & Upload)

### 1. Arduino CLI (권장)

터미널에서 직접 빌드하고 업로드하는 것이 가장 빠르고 정확합니다.

**컴파일 (Compile):**

```bash
arduino-cli compile --fqbn esp32:esp32:esp32s3:CDCOnBoot=cdc,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,PSRAM=opi .
```

**업로드 (Upload):**
_(포트명 `/dev/cu.usbmodem...`은 본인의 환경에 맞게 변경하세요)_

```bash
arduino-cli upload -p /dev/cu.usbmodem2101 --fqbn esp32:esp32:esp32s3:CDCOnBoot=cdc,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,PSRAM=opi .
```

### 2. 라이브러리 의존성

이 프로젝트는 다음 라이브러리를 필요로 합니다.

- **LVGL**: v8.3.x (그래픽 엔진)
- **ESP32 Board Package**: v2.0.11 이상

---

## 📝 개발자 노트 (Dev Notes)

### 로그 리다이렉션 원리

`esp_log_set_vprintf` 함수를 사용하여 ESP32 시스템 레벨에서 발생하는 모든 로그(`ESP_LOGI`, `ESP_LOGE` 등)를 가로챕니다. 가로챈 로그는 `vprintf_to_lvgl` 콜백 함수를 통해 FreeRTOS Queue에 저장되고, 메인 루프에서 안전하게 LVGL 텍스트 영역(Text Area)에 렌더링 됩니다.

```cpp
// 로그 훅(Hook) 등록
esp_log_set_vprintf(vprintf_to_lvgl);
```

### 디렉토리 구조

```
.
├── 54-4_ESP32-LogViewer.ino  # 메인 소스 코드
├── display.h                 # 디스플레이 설정 헤더
├── esp_bsp.c / .h            # 보드 지원 패키지 (초기화)
├── esp_lcd_axs15231b.c / .h  # AXS15231B 드라이버
├── lv_conf.h                 # LVGL 설정 파일
├── lv_port.c / .h            # LVGL 포팅 계층
└── emulator.html             # (Optional) 웹 시뮬레이터
```

---

## 📜 라이선스 (License)

Apache-2.0 License
