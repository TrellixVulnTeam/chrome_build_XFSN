// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#if !defined(__has_feature) || !__has_feature(objc_arc)
#error "This file requires ARC support."
#endif

#import "remoting/ios/domain/host_settings.h"

@implementation HostSettings

@synthesize hostId = _hostId;
@synthesize inputMode = _inputMode;

- (id)initWithCoder:(NSCoder*)coder {
  self = [super init];
  if (self) {
    self.hostId = [coder decodeObjectForKey:@"hostId"];
    NSNumber* mode = [coder decodeObjectForKey:@"inputMode"];
    self.inputMode = (ClientInputMode)[mode intValue];
  }
  return self;
}

- (void)encodeWithCoder:(NSCoder*)coder {
  [coder encodeObject:self.hostId forKey:@"hostId"];
  NSNumber* mode = [NSNumber numberWithInt:self.inputMode];
  [coder encodeObject:mode forKey:@"inputMode"];
}

- (NSString*)description {
  return [NSString stringWithFormat:@"HostSettings: hostId=%@ inputMode=%d",
                                    _hostId, (int)_inputMode];
}

@end
