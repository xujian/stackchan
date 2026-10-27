/*
 * SPDX-License-Identifier: MIT
 */
#include "nap_timer.h"
#include "application.h"
#include <esp_log.h>
#include <esp_random.h>
#include <ctime>

#define TAG "NapTimer"

NapTimer::NapTimer(const Config& config, std::function<void()> onSleep, std::function<void(bool)> onWake,
                   std::function<void(Sound)> onSound)
    : config_(config), on_sleep_(std::move(onSleep)), on_wake_(std::move(onWake)), on_sound_(std::move(onSound))
{
    sleep_after_ = RandomBetween(config_.sleepAfterMinS, config_.sleepAfterMaxS);

    esp_timer_create_args_t args = {
        .callback = [](void* arg) { static_cast<NapTimer*>(arg)->Tick(); },
        .arg      = this,
        .name     = "nap_timer",
    };
    ESP_ERROR_CHECK(esp_timer_create(&args, &timer_));
    ESP_ERROR_CHECK(esp_timer_start_periodic(timer_, 1000000));
    ESP_LOGI(TAG, "Started, first nap after %d s idle", sleep_after_);
}

NapTimer::~NapTimer()
{
    esp_timer_stop(timer_);
    esp_timer_delete(timer_);
}

void NapTimer::WakeUp()
{
    std::lock_guard<std::mutex> lock(mutex_);
    idle_s_ = 0;
    if (asleep_) {
        Wake(false);
    }
}

void NapTimer::Tick()
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto& app = Application::GetInstance();

    if (!asleep_) {
        idle_s_ = app.CanEnterSleepMode() ? idle_s_ + 1 : 0;
        if (idle_s_ >= sleep_after_) {
            asleep_       = true;
            asleep_s_     = 0;
            nap_length_   = RandomBetween(config_.napMinS, config_.napMaxS);
            next_snore_s_ = RandomBetween(config_.snoreMinS, config_.snoreMaxS);
            ESP_LOGI(TAG, "Falling asleep after %d s idle, nap %d s", idle_s_, nap_length_);
            on_sleep_();
            if (!IsNight()) {
                on_sound_(Sound::FallAsleep);
            }
        }
        return;
    }

    // Busy without a WakeUp call, e.g. a state change that doesn't touch the power level.
    // Not CanEnterSleepMode(): our own snoring keeps the audio queue busy and would wake us up
    if (app.GetDeviceState() != kDeviceStateIdle) {
        Wake(false);
        return;
    }

    asleep_s_++;
    if (asleep_s_ >= nap_length_ && !IsNight()) {
        Wake(true);
        on_sound_(Sound::WakeUp);
        return;
    }

    if (asleep_s_ >= next_snore_s_) {
        next_snore_s_ = asleep_s_ + RandomBetween(config_.snoreMinS, config_.snoreMaxS);
        if (!IsNight()) {
            on_sound_(Sound::Snore);
        }
    }
}

// Caller holds mutex_
void NapTimer::Wake(bool byItself)
{
    asleep_      = false;
    idle_s_      = 0;
    sleep_after_ = RandomBetween(config_.sleepAfterMinS, config_.sleepAfterMaxS);
    ESP_LOGI(TAG, "Waking up %s after %d s, next nap after %d s idle", byItself ? "by itself" : "by activity",
             asleep_s_, sleep_after_);
    on_wake_(byItself);
}

bool NapTimer::IsNight() const
{
    time_t now = time(nullptr);
    struct tm local;
    localtime_r(&now, &local);
    if (local.tm_year + 1900 < 2025) {
        return false;  // clock not synced yet, treat as day
    }

    const int h = local.tm_hour;
    if (config_.nightStartHour > config_.nightEndHour) {
        return h >= config_.nightStartHour || h < config_.nightEndHour;  // wraps past midnight
    }
    return h >= config_.nightStartHour && h < config_.nightEndHour;
}

int NapTimer::RandomBetween(int min, int max)
{
    return min + static_cast<int>(esp_random() % static_cast<uint32_t>(max - min + 1));
}
