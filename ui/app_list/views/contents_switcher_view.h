// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UI_APP_LIST_VIEWS_CONTENTS_SWITCHER_VIEW_H_
#define UI_APP_LIST_VIEWS_CONTENTS_SWITCHER_VIEW_H_

#include "base/basictypes.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/view.h"

namespace app_list {

class ContentsView;

// A view that contains buttons to switch the displayed view in the given
// ContentsView.
class ContentsSwitcherView : public views::View, public views::ButtonListener {
 public:
  explicit ContentsSwitcherView(ContentsView* contents_view);
  virtual ~ContentsSwitcherView();

 private:
  // Adds a switcher button using |resource_id| as the button's image and |tag|
  // as the button's id.
  void AddSwitcherButton(int resource_id, int tag);

  // Overridden from views::View:
  virtual gfx::Size GetPreferredSize() OVERRIDE;
  virtual void Layout() OVERRIDE;

  // Overridden from views::ButtonListener:
  virtual void ButtonPressed(views::Button* sender,
                             const ui::Event& event) OVERRIDE;

  ContentsView* contents_view_;  // Owned by views hierarchy.
  views::View* buttons_;         // Owned by views hierarchy.

  DISALLOW_COPY_AND_ASSIGN(ContentsSwitcherView);
};

}  // namespace app_list

#endif  // UI_APP_LIST_VIEWS_CONTENTS_SWITCHER_VIEW_H_
