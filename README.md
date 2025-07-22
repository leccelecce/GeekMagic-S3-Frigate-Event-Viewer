# GeekMagic-S3-Frigate-Event-Viewer
ESP32-S3 firmware for displaying event snapshots from Frigate on a 240x240 display

**This is a fork of https://github.com/Marijn0/GeekMagic-S3-Frigate-Event-Viewer, amended to work on my Wareshare device, and with a number of enhancements**
The primary code change has been to get it working on the ST7789V2 display chip, and also to workaround some issues calling the Frigate HTTP API in my environment where it always seems to time out on the first query.

**Built for:** 
*Waveshare ESP32-S3 LCD 1.3* (based on `esp32-s3-devkitm-1` with 16MB flash + 240x240 TFT display)
This project is designed to be built using [PlatformIO](https://platformio.org/)

A compact and configurable ESP32-S3 firmware for displaying event snapshots from **Frigate**, weather data, and clock information — all in real time on a 240x240 screen.

**Requires a microSD card for storage**

---

Features:

- Live Frigate Notifications via MQTT on ESP32-S3 with automatic image downloading.
- Image Slideshow of recent events with zone labeling, auto-clearing logic, and memory limits.
- Weather Display using OpenWeatherMap API (3.0), including temperature, humidity, min/max temperature, rain/snowfall and icon rendering.
- Clock Display with date, time, and weather info.
- Web Configuration UI: WiFi, MQTT, Frigate IP, weather API key, display settings, and more.
- Persistent Storage using Preferences and SPIFFS to save settings and event images.
- Fallback AP-mode if WiFi is not available.

---

![Image-Clock](https://github.com/user-attachments/assets/957b44dd-ae78-4843-9c73-cd74e6f4e380)



