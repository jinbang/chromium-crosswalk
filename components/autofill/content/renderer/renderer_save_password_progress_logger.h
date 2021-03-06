// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_AUTOFILL_CONTENT_RENDERER_RENDERER_BROWSER_SAVE_PASSWORD_PROGRESS_LOGGER_H_
#define COMPONENTS_AUTOFILL_CONTENT_RENDERER_RENDERER_BROWSER_SAVE_PASSWORD_PROGRESS_LOGGER_H_

#include <string>

#include "components/autofill/core/common/save_password_progress_logger.h"

class PasswordManagerClient;

namespace IPC {
class Sender;
}

namespace autofill {

// This is the SavePasswordProgressLogger specialization for the renderer code,
// which sends logs to the browser process over IPC.
class RendererSavePasswordProgressLogger
    : public autofill::SavePasswordProgressLogger {
 public:
  // The logger will use |sender| and |routing_id| to send a
  // AutofillHostMsg_RecordSavePasswordProgress message with the logs to the
  // browser.
  RendererSavePasswordProgressLogger(IPC::Sender* sender, int routing_id);
  virtual ~RendererSavePasswordProgressLogger();

 protected:
  // SavePasswordProgressLogger:
  virtual void SendLog(const std::string& log) OVERRIDE;

 private:
  // Used by SendLog to send the IPC message with logs. |sender_| needs to
  // outlive the logger.
  IPC::Sender* const sender_;
  const int routing_id_;

  DISALLOW_COPY_AND_ASSIGN(RendererSavePasswordProgressLogger);
};

}  // namespace autofill

#endif  // COMPONENTS_AUTOFILL_CONTENT_RENDERER_RENDERER_BROWSER_SAVE_PASSWORD_PROGRESS_LOGGER_H_
