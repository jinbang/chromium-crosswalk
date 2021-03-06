// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/base/android/ui_base_jni_registrar.h"

#include "base/android/jni_android.h"
#include "base/android/jni_registrar.h"
#include "ui/base/android/view_android.h"
#include "ui/base/android/window_android.h"
#include "ui/base/clipboard/clipboard_android_initialization.h"
#include "ui/base/device_form_factor_android.h"
#include "ui/base/l10n/l10n_util_android.h"
#include "ui/base/touch/touch_device.h"

namespace ui {
namespace android {

static base::android::RegistrationMethod kUiRegisteredMethods[] = {
  { "Clipboard", RegisterClipboardAndroid },
  { "DeviceFormFactor", RegisterDeviceFormFactorAndroid },
  { "LocalizationUtils", l10n_util::RegisterLocalizationUtil },
  { "TouchDevice", RegisterTouchDeviceAndroid },
  { "ViewAndroid", ViewAndroid::RegisterViewAndroid },
  { "WindowAndroid", WindowAndroid::RegisterWindowAndroid },
};

bool RegisterJni(JNIEnv* env) {
  return RegisterNativeMethods(env, kUiRegisteredMethods,
                               arraysize(kUiRegisteredMethods));
}

}  // namespace android
}  // namespace ui
