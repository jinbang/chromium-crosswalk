// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/file_system_provider/provided_file_system.h"

namespace chromeos {
namespace file_system_provider {

ProvidedFileSystemInfo::ProvidedFileSystemInfo() {}

ProvidedFileSystemInfo::ProvidedFileSystemInfo(
    const std::string& extension_id,
    int file_system_id,
    const std::string& file_system_name,
    const base::FilePath& mount_path)
    : extension_id_(extension_id),
      file_system_id_(file_system_id),
      file_system_name_(file_system_name),
      mount_path_(mount_path) {}

ProvidedFileSystemInfo::~ProvidedFileSystemInfo() {}

}  // namespace file_system_provider
}  // namespace chromeos
