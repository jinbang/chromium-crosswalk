// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// On Mac, shortcuts can't have command-line arguments. Instead, produce small
// app bundles which locate the Chromium framework and load it, passing the
// appropriate data. This is the code for such an app bundle. It should be kept
// minimal and do as little work as possible (with as much work done on
// framework side as possible).

#include <dlfcn.h>

#include <CoreFoundation/CoreFoundation.h>
#import <Foundation/Foundation.h>

#include "base/command_line.h"
#include "base/file_util.h"
#include "base/files/file_path.h"
#include "base/logging.h"
#include "base/mac/foundation_util.h"
#include "base/mac/launch_services_util.h"
#include "base/mac/scoped_nsautorelease_pool.h"
#include "base/strings/sys_string_conversions.h"
#import "chrome/common/mac/app_mode_chrome_locator.h"
#include "chrome/common/mac/app_mode_common.h"

namespace {

typedef int (*StartFun)(const app_mode::ChromeAppModeInfo*);

int LoadFrameworkAndStart(app_mode::ChromeAppModeInfo* info) {
  using base::SysNSStringToUTF8;
  using base::SysNSStringToUTF16;
  using base::mac::CFToNSCast;
  using base::mac::CFCastStrict;
  using base::mac::NSToCFCast;

  base::mac::ScopedNSAutoreleasePool scoped_pool;

  // Get the current main bundle, i.e., that of the app loader that's running.
  NSBundle* app_bundle = [NSBundle mainBundle];
  CHECK(app_bundle) << "couldn't get loader bundle";

  // ** 1: Get path to outer Chrome bundle.
  // Get the bundle ID of the browser that created this app bundle.
  NSString* cr_bundle_id = base::mac::ObjCCast<NSString>(
      [app_bundle objectForInfoDictionaryKey:app_mode::kBrowserBundleIDKey]);
  CHECK(cr_bundle_id) << "couldn't get browser bundle ID";

  // First check if Chrome exists at the last known location.
  base::FilePath cr_bundle_path;
  NSString* cr_bundle_path_ns =
      [CFToNSCast(CFCastStrict<CFStringRef>(CFPreferencesCopyAppValue(
          NSToCFCast(app_mode::kLastRunAppBundlePathPrefsKey),
          NSToCFCast(cr_bundle_id)))) autorelease];
  cr_bundle_path = base::mac::NSStringToFilePath(cr_bundle_path_ns);
  bool found_bundle =
      !cr_bundle_path.empty() && base::DirectoryExists(cr_bundle_path);

  if (!found_bundle) {
    // If no such bundle path exists, try to search by bundle ID.
    if (!app_mode::FindBundleById(cr_bundle_id, &cr_bundle_path)) {
      // TODO(jeremy): Display UI to allow user to manually locate the Chrome
      // bundle.
      LOG(FATAL) << "Failed to locate bundle by identifier";
    }
  }

  // ** 2: Read information from the Chrome bundle.
  base::string16 raw_version_str;
  base::FilePath version_path;
  base::FilePath framework_shlib_path;
  if (!app_mode::GetChromeBundleInfo(cr_bundle_path, &raw_version_str,
          &version_path, &framework_shlib_path)) {
    LOG(FATAL) << "Couldn't ready Chrome bundle info";
  }
  base::FilePath app_mode_bundle_path =
      base::mac::NSStringToFilePath([app_bundle bundlePath]);

  // ** 3: Fill in ChromeAppModeInfo.
  info->chrome_outer_bundle_path = cr_bundle_path;
  info->chrome_versioned_path = version_path;
  info->app_mode_bundle_path = app_mode_bundle_path;

  // Read information about the this app shortcut from the Info.plist.
  // Don't check for null-ness on optional items.
  NSDictionary* info_plist = [app_bundle infoDictionary];
  CHECK(info_plist) << "couldn't get loader Info.plist";

  info->app_mode_id = SysNSStringToUTF8(
      [info_plist objectForKey:app_mode::kCrAppModeShortcutIDKey]);
  CHECK(info->app_mode_id.size()) << "couldn't get app shortcut ID";

  info->app_mode_name = SysNSStringToUTF16(
      [info_plist objectForKey:app_mode::kCrAppModeShortcutNameKey]);

  info->app_mode_url = SysNSStringToUTF8(
      [info_plist objectForKey:app_mode::kCrAppModeShortcutURLKey]);

  info->user_data_dir = base::mac::NSStringToFilePath(
      [info_plist objectForKey:app_mode::kCrAppModeUserDataDirKey]);

  info->profile_dir = base::mac::NSStringToFilePath(
      [info_plist objectForKey:app_mode::kCrAppModeProfileDirKey]);

  // Open the framework.
  StartFun ChromeAppModeStart = NULL;
  void* cr_dylib = dlopen(framework_shlib_path.value().c_str(), RTLD_LAZY);
  if (cr_dylib) {
    // Find the entry point.
    ChromeAppModeStart = (StartFun)dlsym(cr_dylib, "ChromeAppModeStart");
    if (!ChromeAppModeStart)
      LOG(ERROR) << "Couldn't get entry point: " << dlerror();
  } else {
    LOG(ERROR) << "Couldn't load framework: " << dlerror();
  }

  if (ChromeAppModeStart) {
    return ChromeAppModeStart(info);
  } else {
    LOG(ERROR) << "Loading Chrome failed, launching with command line.";
    // Launch Chrome instead and have it update this app_mode_loader bundle.
    CommandLine command_line(CommandLine::NO_PROGRAM);
    command_line.AppendSwitchPath(app_mode::kAppShimError,
                                  app_mode_bundle_path);
    if (!base::mac::OpenApplicationWithPath(
            cr_bundle_path, command_line, kLSLaunchDefaults, NULL)) {
      LOG(ERROR) << "Could not launch Chrome from: " << cr_bundle_path.value();
      return 1;
    }

    return 0;
  }
}

} // namespace

__attribute__((visibility("default")))
int main(int argc, char** argv) {
  app_mode::ChromeAppModeInfo info;

  // Hard coded info parameters.
  info.major_version = app_mode::kCurrentChromeAppModeInfoMajorVersion;
  info.minor_version = app_mode::kCurrentChromeAppModeInfoMinorVersion;
  info.argc = argc;
  info.argv = argv;

  // Exit instead of returning to avoid the the removal of |main()| from stack
  // backtraces under tail call optimization.
  exit(LoadFrameworkAndStart(&info));
}
