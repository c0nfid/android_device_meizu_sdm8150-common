/*
 * Copyright (C) 2019-2020 The MoKee Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#define LOG_TAG "LightService"

#include "Light.h"

#include <android-base/logging.h>
#include <fstream>

#define PANEL_BRIGHTNESS_PATH "/sys/class/backlight/panel0-backlight/brightness"
#define PANEL_MAX_BRIGHTNESS_PATH "/sys/class/backlight/panel0-backlight/max_brightness"
#define MX_LED_BLINK_PATH LIGHT_MX_LED_PATH "/blink"

#define LED_OFF 0
#define LED_BLINK 10

namespace {
using android::hardware::light::V2_0::LightState;

static constexpr int DEFAULT_MAX_BRIGHTNESS = 255;

static uint32_t rgbToBrightness(const LightState& state) {
    uint32_t color = state.color & 0x00ffffff;
    return ((77 * ((color >> 16) & 0xff)) + (150 * ((color >> 8) & 0xff)) +
            (29 * (color & 0xff))) >> 8;
}

static bool isLit(const LightState& state) {
    return (state.color & 0x00ffffff);
}
}  // anonymous namespace

namespace android {
namespace hardware {
namespace light {
namespace V2_0 {
namespace implementation {

/*
 * Write value to path and close file.
 */
template <typename T>
static void set(const std::string& path, const T& value) {
    std::ofstream file(path);
    file << value;
}

template <typename T>
static T get(const std::string& path, const T& def) {
    std::ifstream file(path);
    T result;

    file >> result;
    return file.fail() ? def : result;
}

Light::Light() {
    mPanelMaxBrightness = get(PANEL_MAX_BRIGHTNESS_PATH, DEFAULT_MAX_BRIGHTNESS);

    auto attnFn(std::bind(&Light::setAttentionLight, this, std::placeholders::_1));
    auto backlightFn(std::bind(&Light::setPanelBacklight, this, std::placeholders::_1));
    auto notifFn(std::bind(&Light::setNotificationLight, this, std::placeholders::_1));
    mLights.emplace(std::make_pair(Type::ATTENTION, attnFn));
    mLights.emplace(std::make_pair(Type::BACKLIGHT, backlightFn));
    mLights.emplace(std::make_pair(Type::NOTIFICATIONS, notifFn));
}

// Methods from ::android::hardware::light::V2_0::ILight follow.
Return<Status> Light::setLight(Type type, const LightState& state) {
    auto it = mLights.find(type);

    if (it == mLights.end()) {
        return Status::LIGHT_NOT_SUPPORTED;
    }

    it->second(state);

    return Status::SUCCESS;
}

Return<void> Light::getSupportedTypes(getSupportedTypes_cb _hidl_cb) {
    std::vector<Type> types;

    for (auto const& light : mLights) {
        types.push_back(light.first);
    }

    _hidl_cb(types);

    return Void();
}

void Light::setAttentionLight(const LightState& state) {
    std::lock_guard<std::mutex> lock(mLock);
    mAttentionState = state;
    setSpeakerBatteryLightLocked();
}

void Light::setPanelBacklight(const LightState& state) {
    std::lock_guard<std::mutex> lock(mLock);

    uint32_t brightness = rgbToBrightness(state);

    // If max panel brightness is not the default (255),
    // apply linear scaling across the accepted range.
    if (mPanelMaxBrightness != DEFAULT_MAX_BRIGHTNESS) {
        int old_brightness = brightness;
        brightness = brightness * mPanelMaxBrightness / DEFAULT_MAX_BRIGHTNESS;
        LOG(VERBOSE) << "scaling brightness " << old_brightness << " => " << brightness;
    }

    set(PANEL_BRIGHTNESS_PATH, brightness);
}

void Light::setNotificationLight(const LightState& state) {
    std::lock_guard<std::mutex> lock(mLock);
    mNotificationState = state;
    setSpeakerBatteryLightLocked();
}

void Light::setSpeakerBatteryLightLocked() {
    if (isLit(mNotificationState)) {
        set(MX_LED_BLINK_PATH, LED_BLINK);
    } else if (isLit(mAttentionState)) {
        set(MX_LED_BLINK_PATH, LED_BLINK);
    } else {
        set(MX_LED_BLINK_PATH, LED_OFF);
    }
}

}  // namespace implementation
}  // namespace V2_0
}  // namespace light
}  // namespace hardware
}  // namespace android
