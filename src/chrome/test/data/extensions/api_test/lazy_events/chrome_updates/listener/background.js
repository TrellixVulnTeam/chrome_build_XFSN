// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

chrome.runtime.onInstalled.addListener(function(info) {
  if (info.reason == 'chrome_update')
    chrome.test.sendMessage('update event');
});
chrome.test.notifyPass();
