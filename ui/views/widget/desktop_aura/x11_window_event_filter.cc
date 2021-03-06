// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/views/widget/desktop_aura/x11_window_event_filter.h"

#include <X11/extensions/XInput.h>
#include <X11/extensions/XInput2.h>
#include <X11/Xatom.h>
#include <X11/Xlib.h>

#include "ui/aura/client/aura_constants.h"
#include "ui/aura/window.h"
#include "ui/aura/window_delegate.h"
#include "ui/aura/window_tree_host.h"
#include "ui/base/hit_test.h"
#include "ui/events/event.h"
#include "ui/events/event_utils.h"
#include "ui/gfx/x/x11_types.h"
#include "ui/views/linux_ui/linux_ui.h"
#include "ui/views/widget/desktop_aura/desktop_window_tree_host.h"
#include "ui/views/widget/native_widget_aura.h"

namespace {

// These constants are defined in the Extended Window Manager Hints
// standard...and aren't in any header that I can find.
const int k_NET_WM_MOVERESIZE_SIZE_TOPLEFT =     0;
const int k_NET_WM_MOVERESIZE_SIZE_TOP =         1;
const int k_NET_WM_MOVERESIZE_SIZE_TOPRIGHT =    2;
const int k_NET_WM_MOVERESIZE_SIZE_RIGHT =       3;
const int k_NET_WM_MOVERESIZE_SIZE_BOTTOMRIGHT = 4;
const int k_NET_WM_MOVERESIZE_SIZE_BOTTOM =      5;
const int k_NET_WM_MOVERESIZE_SIZE_BOTTOMLEFT =  6;
const int k_NET_WM_MOVERESIZE_SIZE_LEFT =        7;
const int k_NET_WM_MOVERESIZE_MOVE =             8;

// This data structure represents additional hints that we send to the window
// manager and has a direct lineage back to Motif, which defined this de facto
// standard. This struct doesn't seem 64-bit safe though, but it's what GDK
// does.
typedef struct {
  unsigned long flags;
  unsigned long functions;
  unsigned long decorations;
  long input_mode;
  unsigned long status;
} MotifWmHints;

// The bitflag in |flags| in MotifWmHints that signals that the reader should
// pay attention to the value in |decorations|.
const unsigned long kHintsDecorations = (1L << 1);

const char* kAtomsToCache[] = {
  "_MOTIF_WM_HINTS",
  "_NET_WM_MOVERESIZE",
  NULL
};

}  // namespace

namespace views {

X11WindowEventFilter::X11WindowEventFilter(
    DesktopWindowTreeHost* window_tree_host)
    : xdisplay_(gfx::GetXDisplay()),
      xwindow_(window_tree_host->AsWindowTreeHost()->GetAcceleratedWidget()),
      x_root_window_(DefaultRootWindow(xdisplay_)),
      atom_cache_(xdisplay_, kAtomsToCache),
      window_tree_host_(window_tree_host),
      is_active_(false),
      click_component_(HTNOWHERE) {
}

X11WindowEventFilter::~X11WindowEventFilter() {
}

void X11WindowEventFilter::SetUseHostWindowBorders(bool use_os_border) {
  MotifWmHints motif_hints;
  memset(&motif_hints, 0, sizeof(motif_hints));
  motif_hints.flags = kHintsDecorations;
  motif_hints.decorations = use_os_border ? 1 : 0;

  ::Atom hint_atom = atom_cache_.GetAtom("_MOTIF_WM_HINTS");
  XChangeProperty(gfx::GetXDisplay(),
                  xwindow_,
                  hint_atom,
                  hint_atom,
                  32,
                  PropModeReplace,
                  reinterpret_cast<unsigned char*>(&motif_hints),
                  sizeof(MotifWmHints)/sizeof(long));
}

void X11WindowEventFilter::OnMouseEvent(ui::MouseEvent* event) {
  if (event->type() != ui::ET_MOUSE_PRESSED)
    return;

  if (!(event->IsLeftMouseButton() || event->IsMiddleMouseButton()))
    return;

  aura::Window* target = static_cast<aura::Window*>(event->target());
  if (!target->delegate())
    return;

  int previous_click_component = HTNOWHERE;
  int component =
      target->delegate()->GetNonClientComponent(event->location());
  if (event->IsLeftMouseButton()) {
    previous_click_component = click_component_;
    click_component_ = component;
  }
  if (component == HTCLIENT)
    return;

  if (event->IsMiddleMouseButton() && (component == HTCAPTION)) {
    LinuxUI::NonClientMiddleClickAction action =
        LinuxUI::MIDDLE_CLICK_ACTION_LOWER;
    LinuxUI* linux_ui = LinuxUI::instance();
    if (linux_ui)
      action = linux_ui->GetNonClientMiddleClickAction();

    switch (action) {
      case LinuxUI::MIDDLE_CLICK_ACTION_NONE:
        break;
      case LinuxUI::MIDDLE_CLICK_ACTION_LOWER:
        XLowerWindow(xdisplay_, xwindow_);
        break;
      case LinuxUI::MIDDLE_CLICK_ACTION_MINIMIZE:
        window_tree_host_->Minimize();
        break;
      case LinuxUI::MIDDLE_CLICK_ACTION_TOGGLE_MAXIMIZE:
        if (target->GetProperty(aura::client::kCanMaximizeKey))
          ToggleMaximizedState();
        break;
    }

    event->SetHandled();
    return;
  }

  // Left button case.
  if (event->flags() & ui::EF_IS_DOUBLE_CLICK) {
    click_component_ = HTNOWHERE;
    if (component == HTCAPTION &&
        target->GetProperty(aura::client::kCanMaximizeKey) &&
        previous_click_component == component) {
      // Our event is a double click in the caption area in a window that can be
      // maximized. We are responsible for dispatching this as a minimize/
      // maximize on X11 (Windows converts this to min/max events for us).
      ToggleMaximizedState();
      event->SetHandled();
      return;
    }
  }

  // Get the |x_root_window_| location out of the native event.
  if (event->native_event()) {
    const gfx::Point x_root_location =
        ui::EventSystemLocationFromNative(event->native_event());
    if ((component == HTCAPTION ||
         target->GetProperty(aura::client::kCanResizeKey)) &&
        DispatchHostWindowDragMovement(component, x_root_location)) {
      event->StopPropagation();
    }
  }
}

void X11WindowEventFilter::ToggleMaximizedState() {
  if (window_tree_host_->IsMaximized())
    window_tree_host_->Restore();
  else
    window_tree_host_->Maximize();
}

bool X11WindowEventFilter::DispatchHostWindowDragMovement(
    int hittest,
    const gfx::Point& screen_location) {
  int direction = -1;
  switch (hittest) {
    case HTBOTTOM:
      direction = k_NET_WM_MOVERESIZE_SIZE_BOTTOM;
      break;
    case HTBOTTOMLEFT:
      direction = k_NET_WM_MOVERESIZE_SIZE_BOTTOMLEFT;
      break;
    case HTBOTTOMRIGHT:
      direction = k_NET_WM_MOVERESIZE_SIZE_BOTTOMRIGHT;
      break;
    case HTCAPTION:
      direction = k_NET_WM_MOVERESIZE_MOVE;
      break;
    case HTLEFT:
      direction = k_NET_WM_MOVERESIZE_SIZE_LEFT;
      break;
    case HTRIGHT:
      direction = k_NET_WM_MOVERESIZE_SIZE_RIGHT;
      break;
    case HTTOP:
      direction = k_NET_WM_MOVERESIZE_SIZE_TOP;
      break;
    case HTTOPLEFT:
      direction = k_NET_WM_MOVERESIZE_SIZE_TOPLEFT;
      break;
    case HTTOPRIGHT:
      direction = k_NET_WM_MOVERESIZE_SIZE_TOPRIGHT;
      break;
    default:
      return false;
  }

  // We most likely have an implicit grab right here. We need to dump it
  // because what we're about to do is tell the window manager
  // that it's now responsible for moving the window around; it immediately
  // grabs when it receives the event below.
  XUngrabPointer(xdisplay_, CurrentTime);

  XEvent event;
  memset(&event, 0, sizeof(event));
  event.xclient.type = ClientMessage;
  event.xclient.display = xdisplay_;
  event.xclient.window = xwindow_;
  event.xclient.message_type = atom_cache_.GetAtom("_NET_WM_MOVERESIZE");
  event.xclient.format = 32;
  event.xclient.data.l[0] = screen_location.x();
  event.xclient.data.l[1] = screen_location.y();
  event.xclient.data.l[2] = direction;
  event.xclient.data.l[3] = 0;
  event.xclient.data.l[4] = 0;

  XSendEvent(xdisplay_, x_root_window_, False,
             SubstructureRedirectMask | SubstructureNotifyMask,
             &event);

  return true;
}

}  // namespace views
