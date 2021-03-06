// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef EXTENSIONS_RENDERER_USER_SCRIPT_SLAVE_H_
#define EXTENSIONS_RENDERER_USER_SCRIPT_SLAVE_H_

#include <map>
#include <set>
#include <string>
#include <vector>

#include "base/memory/scoped_ptr.h"
#include "base/memory/shared_memory.h"
#include "base/stl_util.h"
#include "base/strings/string_piece.h"
#include "extensions/common/user_script.h"
#include "third_party/WebKit/public/web/WebScriptSource.h"

class GURL;

namespace blink {
class WebFrame;
}

using blink::WebScriptSource;

namespace extensions {
class Extension;
class ExtensionSet;

// Manages installed UserScripts for a render process.
class UserScriptSlave {
 public:
  explicit UserScriptSlave(const ExtensionSet* extensions);
  ~UserScriptSlave();

  // Returns the unique set of extension IDs this UserScriptSlave knows about.
  void GetActiveExtensions(std::set<std::string>* extension_ids);

  // Update the parsed scripts from shared memory.
  bool UpdateScripts(base::SharedMemoryHandle shared_memory);

  // Inject the appropriate scripts into a frame based on its URL.
  // TODO(aa): Extract a UserScriptFrame interface out of this to improve
  // testability.
  void InjectScripts(blink::WebFrame* frame, UserScript::RunLocation location);

  // Gets the isolated world ID to use for the given |extension| in the given
  // |frame|. If no isolated world has been created for that extension,
  // one will be created and initialized.
  int GetIsolatedWorldIdForExtension(const Extension* extension,
                                     blink::WebFrame* frame);

  // Gets the id of the extension running in a given isolated world. If no such
  // isolated world exists, or no extension is running in it, returns empty
  // string.
  std::string GetExtensionIdForIsolatedWorld(int isolated_world_id);

  void RemoveIsolatedWorld(const std::string& extension_id);

 private:
  // Shared memory containing raw script data.
  scoped_ptr<base::SharedMemory> shared_memory_;

  // Parsed script data.
  std::vector<UserScript*> scripts_;
  STLElementDeleter<std::vector<UserScript*> > script_deleter_;

  // Greasemonkey API source that is injected with the scripts.
  base::StringPiece api_js_;

  // Extension metadata.
  const ExtensionSet* extensions_;

  typedef std::map<std::string, int> IsolatedWorldMap;
  IsolatedWorldMap isolated_world_ids_;

  DISALLOW_COPY_AND_ASSIGN(UserScriptSlave);
};

}  // namespace extensions

#endif  // EXTENSIONS_RENDERER_USER_SCRIPT_SLAVE_H_
