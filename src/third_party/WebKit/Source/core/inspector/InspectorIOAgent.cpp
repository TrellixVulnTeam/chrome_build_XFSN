// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "core/inspector/InspectorIOAgent.h"

#include "bindings/core/v8/V8Blob.h"
#include "core/fileapi/Blob.h"
#include "v8/include/v8-inspector.h"

namespace blink {

using protocol::Response;

InspectorIOAgent::InspectorIOAgent(v8::Isolate* isolate,
                                   v8_inspector::V8InspectorSession* session)
    : isolate_(isolate), v8_session_(session) {}

InspectorIOAgent::~InspectorIOAgent() = default;

Response InspectorIOAgent::resolveBlob(const String& object_id, String* uuid) {
  v8::HandleScope handles(isolate_);
  v8::Local<v8::Value> value;
  v8::Local<v8::Context> context;
  std::unique_ptr<v8_inspector::StringBuffer> error;
  if (!v8_session_->unwrapObject(&error, ToV8InspectorStringView(object_id),
                                 &value, &context, nullptr))
    return Response::Error(ToCoreString(std::move(error)));

  if (!V8Blob::hasInstance(value, isolate_))
    return Response::Error("Object id doesn't reference a Blob");

  Blob* blob = V8Blob::toImpl(v8::Local<v8::Object>::Cast(value));
  if (!blob) {
    return Response::Error(
        "Couldn't convert object with given objectId to Blob");
  }

  *uuid = blob->Uuid();
  return Response::OK();
}

}  // namespace blink
