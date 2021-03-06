// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/**
 * @fileoverview
 * @suppress {checkTypes}  By default, JSCompile is not run on test files.
 *    However, you can modify |remoting_webapp_files.gypi| locally to include
 *    the test in the package to expedite local development.  This suppress
 *    is here so that JSCompile won't complain.
 *
 * Provides basic functionality for JavaScript based browser test.
 *
 * To define a browser test, create a class under the browserTest namespace.
 * You can pass arbitrary object literals to the browser test from the C++ test
 * harness as the test data.  Each browser test class should implement the run
 * method.
 * For example:
 *
 * browserTest.My_Test = function(myObjectLiteral) {};
 * browserTest.My_Test.prototype.run() = function() { ... };
 *
 * The browser test is async in nature.  It will keep running until
 * browserTest.fail("My error message.") or browserTest.pass() is called.
 *
 * For example:
 *
 * browserTest.My_Test.prototype.run() = function() {
 *   window.setTimeout(function() {
 *     if (doSomething()) {
 *       browserTest.pass();
 *     } else {
 *       browserTest.fail('My error message.');
 *     }
 *   }, 1000);
 * };
 *
 * You will then invoke the test in C++ by calling:
 *
 *   RunJavaScriptTest(web_content, "My_Test", "{"
 *    "pin: 123123"
 *  "}");
 */

'use strict';

var browserTest = {};

browserTest.init = function() {
  // The domAutomationController is used to communicate progress back to the
  // C++ calling code.  It will only exist if chrome is run with the flag
  // --dom-automation.  It is stubbed out here so that browser test can be run
  // under the regular app.
  browserTest.automationController_ = window.domAutomationController || {
    send: function(json) {
      var result = JSON.parse(json);
      if (result.succeeded) {
        console.log('Test Passed.');
      } else {
        console.error(result.error_message);
      }
    }
  };
};

browserTest.assert = function(expr, message) {
  if (!expr) {
    message = (message) ? '<' + message + '>' : '';
    browserTest.fail('Assertion failed.' + message);
  }
};

browserTest.fail = function(error_message, opt_stack_trace) {
  var stack_trace = opt_stack_trace || base.debug.callstack();

  // To run browserTest locally:
  // 1. Go to |remoting_webapp_files| and look for
  //    |remoting_webapp_js_browser_test_files| and uncomment it
  // 2. gclient runhooks
  // 3. rebuild the webapp
  // 4. Run it in the console browserTest.runTest(browserTest.MyTest, {});
  // 5. The line below will trap the test in the debugger in case of
  //    failure.
  debugger;

  browserTest.automationController_.send(JSON.stringify({
    succeeded: false,
    error_message: error_message,
    stack_trace: stack_trace
  }));
};

browserTest.pass = function() {
  browserTest.automationController_.send(JSON.stringify({
    succeeded: true,
    error_message: '',
    stack_trace: ''
  }));
};

browserTest.clickOnControl = function(id) {
  var element = document.getElementById(id);
  browserTest.assert(element);
  element.click();
};

/** @enum {number} */
browserTest.Timeout = {
  NONE: -1,
  DEFAULT: 5000
};

browserTest.waitForUIMode = function(expectedMode, callback, opt_timeout) {
  var uiModeChanged = remoting.testEvents.Names.uiModeChanged;
  var timerId = null;

  if (opt_timeout === undefined) {
    opt_timeout = browserTest.Timeout.DEFAULT;
  }

  function onTimeout() {
    remoting.testEvents.removeEventListener(uiModeChanged, onUIModeChanged);
    browserTest.fail('Timeout waiting for ' + expectedMode);
  }

  function onUIModeChanged (mode) {
    if (mode == expectedMode) {
      remoting.testEvents.removeEventListener(uiModeChanged, onUIModeChanged);
      window.clearTimeout(timerId);
      timerId = null;
      try {
        callback();
      } catch (e) {
        browserTest.fail(e.toString(), e.stack);
      }
    }
  }

  if (opt_timeout != browserTest.Timeout.NONE) {
    timerId = window.setTimeout(onTimeout.bind(window, timerId), opt_timeout);
  }
  remoting.testEvents.addEventListener(uiModeChanged, onUIModeChanged);
};

browserTest.runTest = function(testClass, data) {
  try {
    var test = new testClass(data);
    browserTest.assert(typeof test.run == 'function');
    test.run();
  } catch (e) {
    browserTest.fail(e.toString(), e.stack);
  }
};

browserTest.init();