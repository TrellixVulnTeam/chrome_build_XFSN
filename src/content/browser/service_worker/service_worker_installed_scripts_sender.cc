// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/service_worker/service_worker_installed_scripts_sender.h"

#include "base/memory/ref_counted.h"
#include "base/trace_event/trace_event.h"
#include "content/browser/service_worker/service_worker_context_core.h"
#include "content/browser/service_worker/service_worker_disk_cache.h"
#include "content/browser/service_worker/service_worker_script_cache_map.h"
#include "content/browser/service_worker/service_worker_storage.h"
#include "content/common/net_adapters.h"
#include "net/http/http_response_headers.h"

namespace content {

namespace {

class MetaDataSender {
 public:
  enum class Status { kSuccess, kFailed };

  MetaDataSender(scoped_refptr<net::IOBufferWithSize> meta_data,
                 mojo::ScopedDataPipeProducerHandle handle)
      : meta_data_(std::move(meta_data)),
        remaining_bytes_(meta_data_->size()),
        handle_(std::move(handle)),
        watcher_(FROM_HERE, mojo::SimpleWatcher::ArmingPolicy::AUTOMATIC),
        weak_factory_(this) {}

  void Start(base::OnceCallback<void(Status)> callback) {
    callback_ = std::move(callback);
    watcher_.Watch(
        handle_.get(), MOJO_HANDLE_SIGNAL_WRITABLE,
        base::Bind(&MetaDataSender::OnWritable, weak_factory_.GetWeakPtr()));
  }

  void OnWritable(MojoResult result) {
    DCHECK_EQ(MOJO_RESULT_OK, result);
    uint32_t size = remaining_bytes_;
    MojoResult rv = mojo::WriteDataRaw(handle_.get(), meta_data_->data(), &size,
                                       MOJO_WRITE_DATA_FLAG_NONE);
    switch (rv) {
      case MOJO_RESULT_INVALID_ARGUMENT:
      case MOJO_RESULT_OUT_OF_RANGE:
      case MOJO_RESULT_BUSY:
        NOTREACHED();
        return;
      case MOJO_RESULT_FAILED_PRECONDITION:
        OnCompleted(Status::kFailed);
        return;
      case MOJO_RESULT_SHOULD_WAIT:
        return;
    }
    remaining_bytes_ -= size;
    if (remaining_bytes_ == 0)
      OnCompleted(Status::kSuccess);
  }

  void OnCompleted(Status status) {
    watcher_.Cancel();
    handle_.reset();
    std::move(callback_).Run(status);
  }

 private:
  base::OnceCallback<void(Status)> callback_;

  scoped_refptr<net::IOBufferWithSize> meta_data_;
  size_t remaining_bytes_;
  mojo::ScopedDataPipeProducerHandle handle_;
  mojo::SimpleWatcher watcher_;

  base::WeakPtrFactory<MetaDataSender> weak_factory_;
};

}  // namespace

// Sender sends a single script to the renderer and calls
// ServiceWorkerIsntalledScriptsSender::OnFinishSendingScript() when done.
class ServiceWorkerInstalledScriptsSender::Sender {
 public:
  Sender(std::unique_ptr<ServiceWorkerResponseReader> reader,
         ServiceWorkerInstalledScriptsSender* owner)
      : reader_(std::move(reader)),
        owner_(owner),
        watcher_(FROM_HERE, mojo::SimpleWatcher::ArmingPolicy::MANUAL),
        weak_factory_(this) {}

  void Start() {
    scoped_refptr<HttpResponseInfoIOBuffer> info_buf =
        base::MakeRefCounted<HttpResponseInfoIOBuffer>();
    reader_->ReadInfo(info_buf.get(), base::Bind(&Sender::OnReadInfoComplete,
                                                 AsWeakPtr(), info_buf));
  }

 private:
  void OnReadInfoComplete(scoped_refptr<HttpResponseInfoIOBuffer> http_info,
                          int result) {
    DCHECK(owner_);
    DCHECK(http_info);
    DCHECK_GT(result, 0);
    if (!http_info->http_info) {
      CompleteSendIfNeeded(Status::kNoHttpInfoError);
      return;
    }

    mojo::ScopedDataPipeConsumerHandle meta_data_consumer;
    mojo::ScopedDataPipeConsumerHandle body_consumer;
    if (mojo::CreateDataPipe(nullptr, &body_handle_, &body_consumer) !=
        MOJO_RESULT_OK) {
      CompleteSendIfNeeded(Status::kCreateDataPipeError);
      return;
    }
    // Start sending meta data (V8 code cache data).
    if (http_info->http_info->metadata) {
      mojo::ScopedDataPipeProducerHandle meta_data_producer;
      if (mojo::CreateDataPipe(nullptr, &meta_data_producer,
                               &meta_data_consumer) != MOJO_RESULT_OK) {
        CompleteSendIfNeeded(Status::kCreateDataPipeError);
        return;
      }
      meta_data_sender_ = base::MakeUnique<MetaDataSender>(
          http_info->http_info->metadata, std::move(meta_data_producer));
      meta_data_sender_->Start(
          base::BindOnce(&Sender::OnMetaDataSent, AsWeakPtr()));
    }

    // Start sending body.
    watcher_.Watch(body_handle_.get(), MOJO_HANDLE_SIGNAL_WRITABLE,
                   base::Bind(&Sender::OnWritableBody, AsWeakPtr()));
    watcher_.ArmOrNotify();

    scoped_refptr<net::HttpResponseHeaders> headers =
        http_info->http_info->headers;
    DCHECK(headers);

    std::string charset;
    headers->GetCharset(&charset);

    // Create a map of response headers.
    std::unordered_map<std::string, std::string> header_strings;
    size_t iter = 0;
    std::string key;
    std::string value;
    // This logic is copied from blink::ResourceResponse::AddHTTPHeaderField.
    while (headers->EnumerateHeaderLines(&iter, &key, &value)) {
      if (header_strings.find(key) == header_strings.end()) {
        header_strings[key] = value;
      } else {
        header_strings[key] += ", " + value;
      }
    }

    owner_->SendScriptInfoToRenderer(charset, std::move(header_strings),
                                     std::move(meta_data_consumer),
                                     std::move(body_consumer));
    owner_->OnHttpInfoRead(http_info);
  }

  void OnWritableBody(MojoResult result) {
    DCHECK_EQ(MOJO_RESULT_OK, result);
    DCHECK(!pending_write_);
    uint32_t num_bytes = 0;
    MojoResult rv = NetToMojoPendingBuffer::BeginWrite(
        &body_handle_, &pending_write_, &num_bytes);
    switch (rv) {
      case MOJO_RESULT_INVALID_ARGUMENT:
      case MOJO_RESULT_BUSY:
        NOTREACHED();
        return;
      case MOJO_RESULT_FAILED_PRECONDITION:
        CompleteSendIfNeeded(Status::kConnectionError);
        return;
      case MOJO_RESULT_SHOULD_WAIT:
        watcher_.ArmOrNotify();
        return;
      case MOJO_RESULT_OK:
        break;
    }

    scoped_refptr<NetToMojoIOBuffer> buffer =
        base::MakeRefCounted<NetToMojoIOBuffer>(pending_write_.get());
    reader_->ReadData(buffer.get(), num_bytes,
                      base::Bind(&Sender::OnResponseDataRead, AsWeakPtr()));
  }

  void OnResponseDataRead(int read_bytes) {
    if (read_bytes < 0) {
      CompleteSendIfNeeded(Status::kResponseReaderError);
      return;
    }
    body_handle_ = pending_write_->Complete(read_bytes);
    DCHECK(body_handle_.is_valid());
    pending_write_ = nullptr;
    if (read_bytes == 0) {
      // All data has been read.
      watcher_.Cancel();
      body_handle_.reset();
      CompleteSendIfNeeded(Status::kSuccess);
      return;
    }
    watcher_.ArmOrNotify();
  }

  void OnMetaDataSent(MetaDataSender::Status status) {
    meta_data_sender_.reset();
    if (status != MetaDataSender::Status::kSuccess) {
      watcher_.Cancel();
      body_handle_.reset();
      CompleteSendIfNeeded(Status::kMetaDataSenderError);
      return;
    }

    CompleteSendIfNeeded(Status::kSuccess);
  }

  // CompleteSendIfNeeded notifies the end of data transfer to |owner_|, and
  // |this| will be removed by |owner_| as a result. Errors are notified
  // immediately, but when the transfer has been succeeded, it's notified when
  // sending both of body and meta data is finished.
  void CompleteSendIfNeeded(Status status) {
    if (status != Status::kSuccess) {
      owner_->OnAbortSendingScript(status);
      return;
    }
    if (!body_handle_.is_valid() && !meta_data_sender_)
      owner_->OnFinishSendingScript();
  }

  base::WeakPtr<Sender> AsWeakPtr() { return weak_factory_.GetWeakPtr(); }

  std::unique_ptr<ServiceWorkerResponseReader> reader_;
  ServiceWorkerInstalledScriptsSender* owner_;

  // For meta data.
  std::unique_ptr<MetaDataSender> meta_data_sender_;

  // For body.
  scoped_refptr<NetToMojoPendingBuffer> pending_write_;
  mojo::SimpleWatcher watcher_;

  // Pipes.
  mojo::ScopedDataPipeProducerHandle meta_data_handle_;
  mojo::ScopedDataPipeProducerHandle body_handle_;

  base::WeakPtrFactory<Sender> weak_factory_;
};

ServiceWorkerInstalledScriptsSender::ServiceWorkerInstalledScriptsSender(
    ServiceWorkerVersion* owner,
    const GURL& main_script_url,
    base::WeakPtr<ServiceWorkerContextCore> context)
    : owner_(owner),
      main_script_url_(main_script_url),
      main_script_id_(kInvalidServiceWorkerResourceId),
      state_(State::kNotStarted),
      context_(std::move(context)) {}

ServiceWorkerInstalledScriptsSender::~ServiceWorkerInstalledScriptsSender() {}

mojom::ServiceWorkerInstalledScriptsInfoPtr
ServiceWorkerInstalledScriptsSender::CreateInfoAndBind() {
  DCHECK_EQ(State::kNotStarted, state_);

  std::vector<ServiceWorkerDatabase::ResourceRecord> resources;
  std::vector<GURL> installed_urls;
  owner_->script_cache_map()->GetResources(&resources);
  for (const auto& resource : resources) {
    installed_urls.emplace_back(resource.url);
    if (resource.url == main_script_url_)
      main_script_id_ = resource.resource_id;
    else
      imported_scripts_.emplace(resource.resource_id, resource.url);
  }

  if (installed_urls.empty())
    return nullptr;

  auto info = mojom::ServiceWorkerInstalledScriptsInfo::New();
  info->manager_request = mojo::MakeRequest(&manager_);
  info->installed_urls = std::move(installed_urls);
  return info;
}

void ServiceWorkerInstalledScriptsSender::Start() {
  DCHECK_EQ(State::kNotStarted, state_);
  // Return if no script has been installed.
  if (main_script_id_ == kInvalidServiceWorkerResourceId) {
    state_ = State::kFinished;
    return;
  }
  TRACE_EVENT_NESTABLE_ASYNC_BEGIN1("ServiceWorker",
                                    "ServiceWorkerInstalledScriptsSender", this,
                                    "main_script_url", main_script_url_.spec());
  state_ = State::kSendingMainScript;
  StartSendingScript(main_script_id_);
}

void ServiceWorkerInstalledScriptsSender::StartSendingScript(
    int64_t resource_id) {
  DCHECK(!running_sender_);
  DCHECK(state_ == State::kSendingMainScript ||
         state_ == State::kSendingImportedScript);
  auto reader = context_->storage()->CreateResponseReader(resource_id);
  TRACE_EVENT_NESTABLE_ASYNC_BEGIN1("ServiceWorker", "SendingScript", this,
                                    "script_url", CurrentSendingURL().spec());
  running_sender_ = base::MakeUnique<Sender>(std::move(reader), this);
  running_sender_->Start();
}

void ServiceWorkerInstalledScriptsSender::SendScriptInfoToRenderer(
    std::string encoding,
    std::unordered_map<std::string, std::string> headers,
    mojo::ScopedDataPipeConsumerHandle meta_data_handle,
    mojo::ScopedDataPipeConsumerHandle body_handle) {
  DCHECK(running_sender_);
  DCHECK(state_ == State::kSendingMainScript ||
         state_ == State::kSendingImportedScript);
  TRACE_EVENT_NESTABLE_ASYNC_INSTANT0("ServiceWorker",
                                      "SendScriptInfoToRenderer", this);
  auto script_info = mojom::ServiceWorkerScriptInfo::New();
  script_info->script_url = CurrentSendingURL();
  script_info->headers = std::move(headers);
  script_info->encoding = std::move(encoding);
  script_info->body = std::move(body_handle);
  script_info->meta_data = std::move(meta_data_handle);
  manager_->TransferInstalledScript(std::move(script_info));
}

void ServiceWorkerInstalledScriptsSender::OnHttpInfoRead(
    scoped_refptr<HttpResponseInfoIOBuffer> http_info) {
  DCHECK(running_sender_);
  DCHECK(state_ == State::kSendingMainScript ||
         state_ == State::kSendingImportedScript);
  if (state_ == State::kSendingMainScript)
    owner_->SetMainScriptHttpResponseInfo(*http_info->http_info);
}

void ServiceWorkerInstalledScriptsSender::OnFinishSendingScript() {
  DCHECK(running_sender_);
  DCHECK(state_ == State::kSendingMainScript ||
         state_ == State::kSendingImportedScript);
  TRACE_EVENT_NESTABLE_ASYNC_END0("ServiceWorker", "SendingScript", this);
  running_sender_.reset();
  if (state_ == State::kSendingMainScript) {
    // Imported scripts are served after the main script.
    imported_script_iter_ = imported_scripts_.begin();
    state_ = State::kSendingImportedScript;
  } else {
    ++imported_script_iter_;
  }
  if (imported_script_iter_ == imported_scripts_.end()) {
    // All scripts have been sent to the renderer.
    // ServiceWorkerInstalledScriptsSender's work is done now.
    DCHECK_EQ(State::kSendingImportedScript, state_);
    state_ = State::kFinished;
    TRACE_EVENT_NESTABLE_ASYNC_END0(
        "ServiceWorker", "ServiceWorkerInstalledScriptsSender", this);
    return;
  }
  // Start sending the next script.
  StartSendingScript(imported_script_iter_->first);
}

void ServiceWorkerInstalledScriptsSender::OnAbortSendingScript(Status status) {
  DCHECK(running_sender_);
  DCHECK(state_ == State::kSendingMainScript ||
         state_ == State::kSendingImportedScript);
  DCHECK_NE(Status::kSuccess, status);
  // TODO(shimazu): Report the error to ServiceWorkerVersion and record its
  // metrics.
  NOTIMPLEMENTED();
}

const GURL& ServiceWorkerInstalledScriptsSender::CurrentSendingURL() {
  if (state_ == State::kSendingMainScript)
    return main_script_url_;
  return imported_script_iter_->second;
}

}  // namespace content
