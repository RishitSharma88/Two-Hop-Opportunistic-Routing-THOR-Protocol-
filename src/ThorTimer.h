// Copyright 2025 Rishit Sharma
// Licensed under the Apache License, Version 2.0
#pragma once
#include <cstdint>

#if defined(_WIN32) || defined(_WIN64)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#if defined(__APPLE__) && defined(__MACH__)
#include <mach/mach_time.h>
#endif

#if defined(ESP_PLATFORM) || defined(ARDUINO_ARCH_ESP32)
#if __has_include("esp_timer.h")
#include "esp_timer.h"
#define THOR_HAS_ESP_TIMER 1
#endif
#endif

#if defined(__ANDROID__) || defined(__linux__) || defined(__APPLE__) || defined(__unix__)
#include <time.h>
#if defined(CLOCK_MONOTONIC)
#define THOR_HAS_CLOCK_GETTIME 1
#endif
#endif

#include <chrono>

// Logical device ids. Must stay in sync with THORConfig::deviceType:
//   0 = Android (default), 1 = ESP32. 2/3 are host-sim targets.
enum ThorDevice : uint8_t {
  THOR_DEVICE_ANDROID = 0,
  THOR_DEVICE_ESP32   = 1,
  THOR_DEVICE_WINDOWS = 2,
  THOR_DEVICE_MACOS   = 3,
  THOR_DEVICE_AUTO    = 255
};

class ThorClock {
public:
  // ---- Portable fallback (steady_clock, monotonic, ns) ----
  static inline uint64_t steadyNanos() {
    auto t = std::chrono::steady_clock::now().time_since_epoch();
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(t).count());
  }

#if defined(_WIN32) || defined(_WIN64)
  // ---- Windows: QueryPerformanceCounter (<1us) ----
  static inline uint64_t qpcNanos() {
    static LARGE_INTEGER freq = [] {
      LARGE_INTEGER f{};
      QueryPerformanceFrequency(&f);
      return f;
    }();
    LARGE_INTEGER c{};
    QueryPerformanceCounter(&c);
    // 128-bit intermediate avoids overflow, keeps ns precision.
    return static_cast<uint64_t>(
        (static_cast<__int128>(c.QuadPart) * 1000000000) / freq.QuadPart);
  }
#endif

#if defined(__APPLE__) && defined(__MACH__)
  // ---- macOS: mach_absolute_time (ns) ----
  static inline uint64_t machNanos() {
    static mach_timebase_info_data_t tb = [] {
      mach_timebase_info_data_t t{};
      mach_timebase_info(&t);
      return t;
    }();
    uint64_t t = mach_absolute_time();
    return t * tb.numer / tb.denom;
  }
#endif

#if defined(THOR_HAS_ESP_TIMER)
  // ---- ESP32: esp_timer_get_time (1us) -> ns ----
  static inline uint64_t espNanos() {
    return static_cast<uint64_t>(esp_timer_get_time()) * 1000ULL;
  }
#endif

#if defined(THOR_HAS_CLOCK_GETTIME)
  // ---- Android/Linux: CLOCK_MONOTONIC (ns) ----
  // Native equivalent of SystemClock.elapsedRealtimeNanos() for the JNI layer.
  static inline uint64_t monotonicNanos() {
    struct timespec ts{};
#if defined(CLOCK_BOOTTIME) && defined(__ANDROID__)
    clock_gettime(CLOCK_BOOTTIME, &ts); // closest to elapsedRealtimeNanos
#else
    clock_gettime(CLOCK_MONOTONIC, &ts);
#endif
    return static_cast<uint64_t>(ts.tv_sec) * 1000000000ULL +
           static_cast<uint64_t>(ts.tv_nsec);
  }
#endif

  // ---- Auto: best available for THIS build host ----
  static inline uint64_t nowNanosAuto() {
#if defined(THOR_HAS_ESP_TIMER) && (defined(ESP_PLATFORM) || defined(ARDUINO_ARCH_ESP32))
    return espNanos();
#elif defined(_WIN32) || defined(_WIN64)
    return qpcNanos();
#elif defined(__APPLE__) && defined(__MACH__)
    return machNanos();
#elif defined(THOR_HAS_CLOCK_GETTIME)
    return monotonicNanos();
#else
    return steadyNanos();
#endif
  }

  // ---- Device-selected entry point: pass cfg.deviceType ----
  // Compile-time guards inside each case keep every target building even
  // when simulating a foreign deviceType on a host (falls back to steady).
  static inline uint64_t nowNanos(uint8_t deviceType) {
    switch (deviceType) {
      case THOR_DEVICE_ESP32:
#if defined(THOR_HAS_ESP_TIMER)
        return espNanos();
#else
        return steadyNanos(); // host simulating ESP32 timing
#endif
      case THOR_DEVICE_WINDOWS:
#if defined(_WIN32) || defined(_WIN64)
        return qpcNanos();
#else
        return steadyNanos(); // non-Windows host fallback
#endif
      case THOR_DEVICE_MACOS:
#if defined(__APPLE__) && defined(__MACH__)
        return machNanos();
#else
        return steadyNanos();
#endif
      case THOR_DEVICE_ANDROID:
      default:
#if defined(THOR_HAS_CLOCK_GETTIME)
        return monotonicNanos();
#else
        return steadyNanos();
#endif
    }
  }

  static inline uint64_t nowNanos() { return nowNanosAuto(); }

  // Convenience derivations (single source, no extra syscalls).
  static inline uint64_t nowMicros(uint8_t deviceType) {
    return nowNanos(deviceType) / 1000ULL;
  }
  static inline uint64_t nowMillis(uint8_t deviceType) {
    return nowNanos(deviceType) / 1000000ULL;
  }
  static inline uint64_t elapsedNanos(uint64_t start, uint64_t end) {
    return end - start; // monotonic u64 wrap-safe
  }
};

// ---- RAII per-function timer (zero heap, ~2x nowNanos cost) ----
// Usage inside any THOR function:
//   ThorScopedTimer t("HelloStateHandler", cfg.deviceType);
// On destruction it reports elapsed ns via the callback (default: none).
// Keep default silent so the hot path stays allocation/log free; pass a
// callback only when you want to record.
#include <functional>

class ThorScopedTimer {
public:
  using Callback = std::function<void(const char*, uint64_t)>;

  explicit ThorScopedTimer(const char* name, uint8_t deviceType,
                           Callback cb = nullptr)
      : name_(name), device_(deviceType), cb_(std::move(cb)),
        start_(ThorClock::nowNanos(deviceType)) {}

  ~ThorScopedTimer() {
    if (cb_) cb_(name_, ThorClock::nowNanos(device_) - start_);
  }

  uint64_t elapsed() const {
    return ThorClock::nowNanos(device_) - start_;
  }
  uint64_t start() const { return start_; }

  ThorScopedTimer(const ThorScopedTimer&) = delete;
  ThorScopedTimer& operator=(const ThorScopedTimer&) = delete;

private:
  const char* name_;
  uint8_t device_;
  Callback cb_;
  uint64_t start_;
};
