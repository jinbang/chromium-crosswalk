// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/extensions/api/bookmark_manager_private/bookmark_manager_private_api.h"

#include "base/command_line.h"
#include "base/prefs/pref_service.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/bookmarks/bookmark_model_factory.h"
#include "chrome/browser/extensions/extension_apitest.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/common/pref_names.h"
#include "components/bookmarks/core/browser/bookmark_model.h"
#include "components/bookmarks/core/test/bookmark_test_helpers.h"
#include "components/user_prefs/user_prefs.h"

// Times out on win syzyasan, http://crbug.com/166026
#if defined(SYZYASAN)
#define MAYBE_BookmarkManager DISABLED_BookmarkManager
#else
#define MAYBE_BookmarkManager BookmarkManager
#endif
IN_PROC_BROWSER_TEST_F(ExtensionApiTest, MAYBE_BookmarkManager) {
  ASSERT_TRUE(RunComponentExtensionTest("bookmark_manager/standard"))
      << message_;
}

IN_PROC_BROWSER_TEST_F(ExtensionApiTest, BookmarkManagerEditDisabled) {
  Profile* profile = browser()->profile();

  // Provide some testing data here, since bookmark editing will be disabled
  // within the extension.
  BookmarkModel* model = BookmarkModelFactory::GetForProfile(profile);
  test::WaitForBookmarkModelToLoad(model);
  const BookmarkNode* bar = model->bookmark_bar_node();
  const BookmarkNode* folder =
      model->AddFolder(bar, 0, base::ASCIIToUTF16("Folder"));
  model->AddURL(bar, 1, base::ASCIIToUTF16("AAA"),
                GURL("http://aaa.example.com"));
  model->AddURL(folder, 0, base::ASCIIToUTF16("BBB"),
                GURL("http://bbb.example.com"));

  PrefService* prefs = user_prefs::UserPrefs::Get(profile);
  prefs->SetBoolean(prefs::kEditBookmarksEnabled, false);

  ASSERT_TRUE(RunComponentExtensionTest("bookmark_manager/edit_disabled"))
      << message_;
}
