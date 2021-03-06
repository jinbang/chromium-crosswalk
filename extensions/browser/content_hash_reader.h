// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef EXTENSIONS_BROWSER_CONTENT_HASH_READER_H_
#define EXTENSIONS_BROWSER_CONTENT_HASH_READER_H_

#include <string>
#include <vector>

#include "base/files/file_path.h"
#include "base/memory/ref_counted.h"
#include "base/memory/scoped_ptr.h"
#include "base/version.h"
#include "extensions/browser/content_verifier_delegate.h"

namespace extensions {

class VerifiedContents;

// This class creates an object that will read expected hashes that may have
// been fetched/calculated by the ContentHashFetcher, and vends them out for
// use in ContentVerifyJob's.
class ContentHashReader : public base::RefCountedThreadSafe<ContentHashReader> {
 public:
  // Create one of these to get expected hashes for the file at |relative_path|
  // within an extension.
  ContentHashReader(const std::string& extension_id,
                    const base::Version& extension_version,
                    const base::FilePath& extension_root,
                    const base::FilePath& relative_path,
                    const ContentVerifierKey& key);

  const std::string& extension_id() const { return extension_id_; }
  const base::FilePath& relative_path() const { return relative_path_; }

  // This should be called to initialize this object (reads the expected hashes
  // from storage, etc.). Must be called on a thread that is allowed to do file
  // I/O. Returns a boolean indicating success/failure. On failure, this object
  // should likely be discarded.
  bool Init();

  // Return the number of blocks and block size, respectively. Only valid after
  // calling Init().
  int block_count() const;
  int block_size() const;

  // Returns a pointer to the expected sha256 hash value for the block at the
  // given index. Only valid after calling Init().
  bool GetHashForBlock(int block_index, const std::string** result) const;

 private:
  friend class base::RefCountedThreadSafe<ContentHashReader>;
  virtual ~ContentHashReader();

  enum InitStatus { NOT_INITIALIZED, SUCCESS, FAILURE };

  std::string extension_id_;
  base::Version extension_version_;
  base::FilePath extension_root_;
  base::FilePath relative_path_;
  ContentVerifierKey key_;

  InitStatus status_;

  // The blocksize used for generating the hashes.
  int block_size_;

  scoped_ptr<VerifiedContents> verified_contents_;

  std::vector<std::string> hashes_;

  DISALLOW_COPY_AND_ASSIGN(ContentHashReader);
};

}  // namespace extensions

#endif  // EXTENSIONS_BROWSER_CONTENT_HASH_READER_H_
