// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "ios/chrome/browser/ntp_tiles/most_visited_sites_observer_bridge.h"

#if !defined(__has_feature) || !__has_feature(objc_arc)
#error "This file requires ARC support."
#endif

namespace ntp_tiles {

MostVisitedSitesObserverBridge::MostVisitedSitesObserverBridge(
    id<MostVisitedSitesObserving> observer) {
  observer_.reset(observer);
}

MostVisitedSitesObserverBridge::~MostVisitedSitesObserverBridge() {}

void MostVisitedSitesObserverBridge::OnMostVisitedURLsAvailable(
    const NTPTilesVector& most_visited) {
  [observer_ onMostVisitedURLsAvailable:most_visited];
}

void MostVisitedSitesObserverBridge::OnIconMadeAvailable(const GURL& site_url) {
  [observer_ onIconMadeAvailable:site_url];
}

}  // namespace ntp_tiles
