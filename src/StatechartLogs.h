/**
 * SPDX-FileCopyrightText: 2026 Maximiliano Ramirez <maximiliano.ramirezbravo@gmail.com>
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#ifdef ESP32
#  include <esp_log.h>
#else
#  include <HardwareSerial.h>
#endif

// Log levels: 0=None, 1=Error, 2=Warning, 3=Info, 4=Debug, 5=Verbose
#ifndef STATECHART_LOG_LEVEL
#  define STATECHART_LOG_LEVEL 0
#endif

// Log output, default: Serial
#ifndef STATECHART_LOG_SERIAL
#  define STATECHART_LOG_SERIAL Serial
#endif

// Log tag
#ifndef STATECHART_LOG_TAG
#  define STATECHART_LOG_TAG "Statechart"
#endif

// Use ESP32 logging function when using ESP32, default is enabled
#ifndef STATECHART_USE_ESP32_LOGS
#  define STATECHART_USE_ESP32_LOGS 1
#endif

// Error
#if STATECHART_LOG_LEVEL >= 1
#  if defined(ESP32) && STATECHART_USE_ESP32_LOGS
#    define STATECHART_LOGE(format, ...) ESP_LOGE(STATECHART_LOG_TAG, format, ##__VA_ARGS__)
#  else
#    define STATECHART_LOGE(format, ...) \
      STATECHART_LOG_SERIAL.printf("E %s: " format "\r\n", STATECHART_LOG_TAG, ##__VA_ARGS__)
#  endif
#else
#  define STATECHART_LOGE(format, ...) void(0)
#endif

// Warning
#if STATECHART_LOG_LEVEL >= 2
#  if defined(ESP32) && STATECHART_USE_ESP32_LOGS
#    define STATECHART_LOGW(format, ...) ESP_LOGW(STATECHART_LOG_TAG, format, ##__VA_ARGS__)
#  else
#    define STATECHART_LOGW(format, ...) \
      STATECHART_LOG_SERIAL.printf("W %s: " format "\r\n", STATECHART_LOG_TAG, ##__VA_ARGS__)
#  endif
#else
#  define STATECHART_LOGW(format, ...) void(0)
#endif

// Info
#if STATECHART_LOG_LEVEL >= 3
#  if defined(ESP32) && STATECHART_USE_ESP32_LOGS
#    define STATECHART_LOGI(format, ...) ESP_LOGI(STATECHART_LOG_TAG, format, ##__VA_ARGS__)
#  else
#    define STATECHART_LOGI(format, ...) \
      STATECHART_LOG_SERIAL.printf("I %s: " format "\r\n", STATECHART_LOG_TAG, ##__VA_ARGS__)
#  endif
#else
#  define STATECHART_LOGI(format, ...) void(0)
#endif

// Debug
#if STATECHART_LOG_LEVEL >= 4
#  if defined(ESP32) && STATECHART_USE_ESP32_LOGS
#    define STATECHART_LOGD(format, ...) ESP_LOGD(STATECHART_LOG_TAG, format, ##__VA_ARGS__)
#  else
#    define STATECHART_LOGD(format, ...) \
      STATECHART_LOG_SERIAL.printf("D %s: " format "\r\n", STATECHART_LOG_TAG, ##__VA_ARGS__)
#  endif
#else
#  define STATECHART_LOGD(format, ...) void(0)
#endif

// Verbose
#if STATECHART_LOG_LEVEL >= 5
#  if defined(ESP32) && STATECHART_USE_ESP32_LOGS
#    define STATECHART_LOGV(format, ...) ESP_LOGV(STATECHART_LOG_TAG, format, ##__VA_ARGS__)
#  else
#    define STATECHART_LOGV(format, ...) \
      STATECHART_LOG_SERIAL.printf("V %s: " format "\r\n", STATECHART_LOG_TAG, ##__VA_ARGS__)
#  endif
#else
#  define STATECHART_LOGV(format, ...) void(0)
#endif