// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/shell/browser/api/shell/shell_api.h"

#include "apps/shell/browser/shell_app_window.h"
#include "apps/shell/browser/shell_desktop_controller.h"
#include "apps/shell/common/api/shell.h"
#include "base/memory/scoped_ptr.h"
#include "base/values.h"
#include "extensions/browser/event_router.h"
#include "extensions/common/extension.h"

using base::DictionaryValue;
using base::ListValue;

namespace CreateWindow = apps::shell_api::shell::CreateWindow;
namespace OnLaunched = apps::shell_api::shell::OnLaunched;

namespace apps {
namespace {

const char kInvalidArguments[] = "Invalid arguments";

// Creates a function call result to send to the renderer.
DictionaryValue* CreateResult(apps::ShellAppWindow* app_window) {
  int view_id = app_window->GetRenderViewRoutingID();

  DictionaryValue* result = new DictionaryValue;
  result->Set("viewId", new base::FundamentalValue(view_id));
  return result;
}

}  // namespace

// static
void ShellAPI::DispatchOnLaunchedEvent(extensions::EventRouter* event_router,
                                       const extensions::Extension* extension) {
  scoped_ptr<DictionaryValue> launch_data(new DictionaryValue());
  scoped_ptr<ListValue> event_args(new ListValue());
  event_args->Append(launch_data.release());
  scoped_ptr<extensions::Event> event(
      new extensions::Event(OnLaunched::kEventName, event_args.Pass()));
  event_router->DispatchEventWithLazyListener(extension->id(), event.Pass());
}

ShellCreateWindowFunction::ShellCreateWindowFunction() {
}

ShellCreateWindowFunction::~ShellCreateWindowFunction() {
}

ExtensionFunction::ResponseAction ShellCreateWindowFunction::Run() {
  scoped_ptr<CreateWindow::Params> params(CreateWindow::Params::Create(*args_));
  EXTENSION_FUNCTION_VALIDATE(params.get());

  // Convert "main.html" to "chrome-extension:/<id>/main.html".
  GURL url = GetExtension()->GetResourceURL(params->url);
  if (!url.is_valid())
    return RespondNow(Error(kInvalidArguments));

  // The desktop keeps ownership of the window.
  apps::ShellAppWindow* app_window =
      apps::ShellDesktopController::instance()->CreateAppWindow(
          browser_context());
  app_window->LoadURL(url);

  // Create the reply to send to the renderer.
  return RespondNow(SingleArgument(CreateResult(app_window)));
}

}  // namespace apps
