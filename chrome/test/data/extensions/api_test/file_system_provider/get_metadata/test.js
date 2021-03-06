// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

'use strict';

var fileSystemId;
var fileSystem;

/**
 * @type {Object}
 * @const
 */
var TESTING_ROOT = Object.freeze({
  isDirectory: true,
  name: '',
  size: 0,
  modificationTime: new Date(2013, 3, 27, 9, 38, 14)
});

/**
 * @type {Object}
 * @const
 */
var TESTING_FILE = Object.freeze({
  isDirectory: false,
  name: 'tiramisu.txt',
  size: 4096,
  modificationTime: new Date(2014, 4, 28, 10, 39, 15)
});

/**
 * Returns metadata for a requested entry.
 *
 * @param {number} inFileSystemId ID of the file system.
 * @param {string} entryPath Path of the requested entry.
 * @param {function(Object)} onSuccess Success callback with metadata passed
 *     an argument.
 * @param {function(string)} onError Error callback with an error code.
 */
function onGetMetadataRequested(
    inFileSystemId, entryPath, onSuccess, onError) {
  if (inFileSystemId != fileSystemId) {
    onError('SECURITY_ERROR');  // enum ProviderError.
    return;
  }

  if (entryPath == '/') {
    onSuccess(TESTING_ROOT);
    return;
  }

  if (entryPath == '/' + TESTING_FILE.name) {
    onSuccess(TESTING_FILE);
    return;
  }

  onError('NOT_FOUND');  // enum ProviderError.
}

/**
 * Sets up the tests. Called once per all test cases. In case of a failure,
 * the callback is not called.
 *
 * @param {function()} callback Success callback.
 */
function setUp(callback) {
  chrome.fileSystemProvider.mount('chocolate.zip', function(id) {
    fileSystemId = id;
    chrome.fileSystemProvider.onGetMetadataRequested.addListener(
        onGetMetadataRequested);
    var volumeId =
        'provided:' + chrome.runtime.id + '-' + fileSystemId + '-user';

    chrome.fileBrowserPrivate.requestFileSystem(
        volumeId,
        function(inFileSystem) {
          chrome.test.assertTrue(!!inFileSystem);

          fileSystem = inFileSystem;
          callback();
        });
  }, function() {
    chrome.test.fail();
  });
}

/**
 * Runs all of the test cases, one by one.
 */
function runTests() {
  chrome.test.runTests([
    // Read metadata of the root.
    function getFileMetadataSuccess() {
      var onSuccess = chrome.test.callbackPass(function() {});
      fileSystem.root.getMetadata(
        function(metadata) {
          chrome.test.assertEq(TESTING_ROOT.size, metadata.size);
          chrome.test.assertEq(
              TESTING_ROOT.modificationTime.toString(),
              metadata.modificationTime.toString());
          onSuccess();
        }, function(error) {
          chrome.test.fail(error.name);
        });
    },
    // Read metadata of an existing testing file.
    function getFileMetadataSuccess() {
      var onSuccess = chrome.test.callbackPass(function() {});
      fileSystem.root.getFile(
          TESTING_FILE.name,
          {create: false},
          function(fileEntry) {
            chrome.test.assertEq(TESTING_FILE.name, fileEntry.name);
            chrome.test.assertEq(
                TESTING_FILE.isDirectory, fileEntry.isDirectory);
            fileEntry.getMetadata(function(metadata) {
              chrome.test.assertEq(TESTING_FILE.size, metadata.size);
              chrome.test.assertEq(
                  TESTING_FILE.modificationTime.toString(),
                  metadata.modificationTime.toString());
              onSuccess();
            }, function(error) {
              chrome.test.fail(error.name);
            });
          },
          function(error) {
            chrome.test.fail(error.name);
          });
    },
    // Read metadata of a directory which does not exist, what should return an
    // error. DirectoryEntry.getDirectory() causes fetching metadata.
    function getFileMetadataNotFound() {
      var onSuccess = chrome.test.callbackPass(function() {});
      fileSystem.root.getDirectory(
          'cranberries',
          {create: false},
          function(dirEntry) {
            chrome.test.fail();
          },
          function(error) {
            chrome.test.assertEq('NotFoundError', error.name);
            onSuccess();
          });
    },
    // Read metadata of a file using getDirectory(). An error should be returned
    // because of type mismatching. DirectoryEntry.getDirectory() causes
    // fetching metadata.
    function getFileMetadataWrongType() {
      var onSuccess = chrome.test.callbackPass(function() {});
      fileSystem.root.getDirectory(
          TESTING_FILE.name,
          {create: false},
          function(fileEntry) {
            chrome.test.fail();
          },
          function(error) {
            chrome.test.assertEq('TypeMismatchError', error.name);
            onSuccess();
          });
    }
  ]);
}

// Setup and run all of the test cases.
setUp(runTests);
