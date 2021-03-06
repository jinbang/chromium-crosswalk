// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_SYNC_DRIVER_FAKE_GENERIC_CHANGE_PROCESSOR_H_
#define COMPONENTS_SYNC_DRIVER_FAKE_GENERIC_CHANGE_PROCESSOR_H_

#include "components/sync_driver/generic_change_processor.h"

#include "components/sync_driver/generic_change_processor_factory.h"
#include "sync/api/sync_error.h"

namespace browser_sync {

// A fake GenericChangeProcessor that can return arbitrary values.
class FakeGenericChangeProcessor : public GenericChangeProcessor {
 public:
  FakeGenericChangeProcessor();
  virtual ~FakeGenericChangeProcessor();

  // Setters for GenericChangeProcessor implementation results.
  void set_sync_model_has_user_created_nodes(bool has_nodes);
  void set_sync_model_has_user_created_nodes_success(bool success);

  // GenericChangeProcessor implementations.
  virtual syncer::SyncError ProcessSyncChanges(
      const tracked_objects::Location& from_here,
      const syncer::SyncChangeList& change_list) OVERRIDE;
  virtual syncer::SyncError GetAllSyncDataReturnError(
      syncer::ModelType type,
      syncer::SyncDataList* data) const OVERRIDE;
  virtual bool GetDataTypeContext(syncer::ModelType type,
                                  std::string* context) const OVERRIDE;
  virtual int GetSyncCountForType(syncer::ModelType type) OVERRIDE;
  virtual bool SyncModelHasUserCreatedNodes(syncer::ModelType type,
                                            bool* has_nodes) OVERRIDE;
  virtual bool CryptoReadyIfNecessary(syncer::ModelType type) OVERRIDE;

 private:
  bool sync_model_has_user_created_nodes_;
  bool sync_model_has_user_created_nodes_success_;
};

// Define a factory for FakeGenericChangeProcessor for convenience.
class FakeGenericChangeProcessorFactory : public GenericChangeProcessorFactory {
 public:
  explicit FakeGenericChangeProcessorFactory(
      scoped_ptr<FakeGenericChangeProcessor> processor);
  virtual ~FakeGenericChangeProcessorFactory();
  virtual scoped_ptr<GenericChangeProcessor> CreateGenericChangeProcessor(
      syncer::UserShare* user_share,
      browser_sync::DataTypeErrorHandler* error_handler,
      const base::WeakPtr<syncer::SyncableService>& local_service,
      const base::WeakPtr<syncer::SyncMergeResult>& merge_result,
      scoped_ptr<syncer::AttachmentService> attachment_service) OVERRIDE;
 private:
  scoped_ptr<FakeGenericChangeProcessor> processor_;
  DISALLOW_COPY_AND_ASSIGN(FakeGenericChangeProcessorFactory);
};

}  // namespace browser_sync

#endif  // COMPONENTS_SYNC_DRIVER_FAKE_GENERIC_CHANGE_PROCESSOR_H_
