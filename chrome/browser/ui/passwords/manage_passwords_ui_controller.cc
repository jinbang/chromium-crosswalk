// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/passwords/manage_passwords_ui_controller.h"

#include "chrome/app/chrome_command_ids.h"
#include "chrome/browser/chrome_notification_types.h"
#include "chrome/browser/password_manager/password_store_factory.h"
#include "chrome/browser/ui/browser_command_controller.h"
#include "chrome/browser/ui/browser_finder.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/ui/chrome_pages.h"
#include "chrome/browser/ui/omnibox/location_bar.h"
#include "chrome/browser/ui/passwords/manage_passwords_icon.h"
#include "chrome/common/url_constants.h"
#include "components/password_manager/core/browser/password_store.h"
#include "content/public/browser/notification_service.h"

using autofill::PasswordFormMap;
using password_manager::PasswordFormManager;

namespace {

password_manager::PasswordStore* GetPasswordStore(
    content::WebContents* web_contents) {
  return PasswordStoreFactory::GetForProfile(
             Profile::FromBrowserContext(web_contents->GetBrowserContext()),
             Profile::EXPLICIT_ACCESS).get();
}

} // namespace

DEFINE_WEB_CONTENTS_USER_DATA_KEY(ManagePasswordsUIController);

ManagePasswordsUIController::ManagePasswordsUIController(
    content::WebContents* web_contents)
    : content::WebContentsObserver(web_contents),
      state_(password_manager::ui::INACTIVE_STATE) {
  password_manager::PasswordStore* password_store =
      GetPasswordStore(web_contents);
  if (password_store)
    password_store->AddObserver(this);
}

ManagePasswordsUIController::~ManagePasswordsUIController() {}

void ManagePasswordsUIController::UpdateBubbleAndIconVisibility() {
  #if !defined(OS_ANDROID)
    Browser* browser = chrome::FindBrowserWithWebContents(web_contents());
    if (!browser)
      return;
    LocationBar* location_bar = browser->window()->GetLocationBar();
    DCHECK(location_bar);
    location_bar->UpdateManagePasswordsIconAndBubble();
  #endif
}

void ManagePasswordsUIController::OnPasswordSubmitted(
    PasswordFormManager* form_manager) {
  form_manager_.reset(form_manager);
  password_form_map_ = form_manager_->best_matches();
  origin_ = PendingCredentials().origin;
  state_ = password_manager::ui::PENDING_PASSWORD_AND_BUBBLE_STATE;
  UpdateBubbleAndIconVisibility();
}

void ManagePasswordsUIController::OnPasswordAutofilled(
    const PasswordFormMap& password_form_map) {
  password_form_map_ = password_form_map;
  origin_ = password_form_map_.begin()->second->origin;
  state_ = password_manager::ui::MANAGE_STATE;
  UpdateBubbleAndIconVisibility();
}

void ManagePasswordsUIController::OnBlacklistBlockedAutofill(
    const PasswordFormMap& password_form_map) {
  password_form_map_ = password_form_map;
  origin_ = password_form_map_.begin()->second->origin;
  state_ = password_manager::ui::BLACKLIST_STATE;
  UpdateBubbleAndIconVisibility();
}

void ManagePasswordsUIController::WebContentsDestroyed() {
  password_manager::PasswordStore* password_store =
      GetPasswordStore(web_contents());
  if (password_store)
    password_store->RemoveObserver(this);
}

void ManagePasswordsUIController::OnLoginsChanged(
    const password_manager::PasswordStoreChangeList& changes) {
  for (password_manager::PasswordStoreChangeList::const_iterator it =
           changes.begin();
       it != changes.end();
       it++) {
    const autofill::PasswordForm& changed_form = it->form();
    if (changed_form.origin != origin_)
      continue;

    if (it->type() == password_manager::PasswordStoreChange::REMOVE) {
      password_form_map_.erase(changed_form.username_value);
    } else {
      autofill::PasswordForm* new_form =
          new autofill::PasswordForm(changed_form);
      password_form_map_[changed_form.username_value] = new_form;
    }
  }
}

void ManagePasswordsUIController::
    NavigateToPasswordManagerSettingsPage() {
// TODO(mkwst): chrome_pages.h is compiled out of Android. Need to figure out
// how this navigation should work there.
#if !defined(OS_ANDROID)
  chrome::ShowSettingsSubPage(
      chrome::FindBrowserWithWebContents(web_contents()),
      chrome::kPasswordManagerSubPage);
#endif
}

void ManagePasswordsUIController::SavePassword() {
  DCHECK(PasswordPendingUserDecision());
  DCHECK(form_manager_.get());
  form_manager_->Save();
  state_ = password_manager::ui::MANAGE_STATE;
}

void ManagePasswordsUIController::NeverSavePassword() {
  DCHECK(PasswordPendingUserDecision());
  DCHECK(form_manager_.get());
  form_manager_->PermanentlyBlacklist();
  state_ = password_manager::ui::BLACKLIST_STATE;
  UpdateBubbleAndIconVisibility();
}

void ManagePasswordsUIController::UnblacklistSite() {
  // We're in one of two states: either the user _just_ blacklisted the site
  // by clicking "Never save" in the pending bubble, or the user is visiting
  // a blacklisted site.
  //
  // Either way, |password_form_map_| has been populated with the relevant
  // form. We can safely pull it out, send it over to the password store
  // for removal, and update our internal state.
  DCHECK(!password_form_map_.empty());
  DCHECK(state_ == password_manager::ui::BLACKLIST_STATE);
  password_manager::PasswordStore* password_store =
      GetPasswordStore(web_contents());
  if (password_store)
    password_store->RemoveLogin(*password_form_map_.begin()->second);
  state_ = password_manager::ui::MANAGE_STATE;
  UpdateBubbleAndIconVisibility();
}

void ManagePasswordsUIController::DidNavigateMainFrame(
    const content::LoadCommittedDetails& details,
    const content::FrameNavigateParams& params) {
  if (details.is_in_page)
    return;
  state_ = password_manager::ui::INACTIVE_STATE;
  UpdateBubbleAndIconVisibility();
}

const autofill::PasswordForm& ManagePasswordsUIController::
    PendingCredentials() const {
  DCHECK(form_manager_);
  return form_manager_->pending_credentials();
}

void ManagePasswordsUIController::UpdateIconAndBubbleState(
    ManagePasswordsIcon* icon) {
  if (state_ == password_manager::ui::PENDING_PASSWORD_AND_BUBBLE_STATE) {
    // We must display the icon before showing the bubble, as the bubble would
    // be otherwise unanchored. However, we can't change the controller's state
    // until _after_ the bubble is shown, as our metrics depend on the
    // distinction between PENDING_PASSWORD_AND_BUBBLE_STATE and
    // PENDING_PASSWORD_STATE to determine if the bubble opened automagically
    // or via user action.
    icon->SetState(password_manager::ui::PENDING_PASSWORD_STATE);
    ShowBubbleWithoutUserInteraction();
    state_ = password_manager::ui::PENDING_PASSWORD_STATE;
  } else  {
    icon->SetState(state_);
  }
}

void ManagePasswordsUIController::ShowBubbleWithoutUserInteraction() {
  DCHECK_EQ(state_, password_manager::ui::PENDING_PASSWORD_AND_BUBBLE_STATE);
#if !defined(OS_ANDROID)
  Browser* browser = chrome::FindBrowserWithWebContents(web_contents());
  if (!browser || browser->toolbar_model()->input_in_progress())
    return;
  CommandUpdater* updater = browser->command_controller()->command_updater();
  updater->ExecuteCommand(IDC_MANAGE_PASSWORDS_FOR_PAGE);
#endif
}

bool ManagePasswordsUIController::PasswordPendingUserDecision() const {
  return state_ == password_manager::ui::PENDING_PASSWORD_STATE ||
         state_ == password_manager::ui::PENDING_PASSWORD_AND_BUBBLE_STATE;
}
