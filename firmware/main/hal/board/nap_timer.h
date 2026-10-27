/*
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <esp_timer.h>
#include <functional>
#include <mutex>

/**
 * @brief Idle naps for the avatar
 *
 * Falls asleep after a random idle time and wakes up by itself after a random nap, except at night.
 * Talking to it (WakeUp) always wakes it up. Runs on external power too; idle shutdown on battery
 * is still handled by xiaozhi's PowerSaveTimer
 */
class NapTimer {
public:
    struct Config {
        int sleepAfterMinS, sleepAfterMaxS;  // idle time before falling asleep
        int napMinS, napMaxS;                // nap length before waking up by itself
        int nightStartHour, nightEndHour;    // local time, no waking up by itself and no sounds in [start, end)
        int snoreMinS, snoreMaxS;            // gap between snores while asleep
    };

    enum class Sound { FallAsleep, Snore, WakeUp };

    // onWake(bool byItself): true when the nap simply ended, false when woken by activity
    // onSound: when to make a nap sound, daytime only. Not fired when woken by activity
    NapTimer(const Config& config, std::function<void()> onSleep, std::function<void(bool)> onWake,
             std::function<void(Sound)> onSound);
    ~NapTimer();

    // Activity (xiaozhi is busy): reset the idle count, wake up if asleep. Safe to call from any task
    void WakeUp();

private:
    Config config_;
    std::function<void()> on_sleep_;
    std::function<void(bool)> on_wake_;
    std::function<void(Sound)> on_sound_;
    esp_timer_handle_t timer_ = nullptr;
    std::mutex mutex_;

    bool asleep_      = false;
    int idle_s_       = 0;
    int asleep_s_     = 0;
    int sleep_after_  = 0;
    int nap_length_   = 0;
    int next_snore_s_ = 0;

    void Tick();
    void Wake(bool byItself);
    bool IsNight() const;
    static int RandomBetween(int min, int max);
};
