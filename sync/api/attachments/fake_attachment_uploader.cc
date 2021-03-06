// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "sync/api/attachments/fake_attachment_uploader.h"

#include "base/bind.h"
#include "base/message_loop/message_loop.h"
#include "sync/api/attachments/attachment.h"

namespace syncer {

FakeAttachmentUploader::FakeAttachmentUploader() {
  DCHECK(CalledOnValidThread());
}

FakeAttachmentUploader::~FakeAttachmentUploader() {
  DCHECK(CalledOnValidThread());
}

void FakeAttachmentUploader::UploadAttachment(const Attachment& attachment,
                                              const UploadCallback& callback) {
  DCHECK(CalledOnValidThread());
  UploadResult result = UPLOAD_SUCCESS;
  AttachmentId updated_id = attachment.GetId();
  // TODO(maniscalco): Update the attachment id with server address information
  // before passing it to the callback.
  base::MessageLoop::current()->PostTask(
      FROM_HERE, base::Bind(callback, result, updated_id));
}

}  // namespace syncer
