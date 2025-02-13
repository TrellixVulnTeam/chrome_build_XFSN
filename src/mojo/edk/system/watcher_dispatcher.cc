// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mojo/edk/system/watcher_dispatcher.h"

#include <algorithm>
#include <limits>
#include <map>

#include "base/macros.h"
#include "base/memory/ptr_util.h"
#include "mojo/edk/system/watch.h"

namespace mojo {
namespace edk {

WatcherDispatcher::WatcherDispatcher(MojoWatcherCallback callback)
    : callback_(callback) {}

void WatcherDispatcher::NotifyHandleState(Dispatcher* dispatcher,
                                          const HandleSignalsState& state) {
  base::AutoLock lock(lock_);
  auto it = watched_handles_.find(dispatcher);
  if (it == watched_handles_.end())
    return;

  // Maybe fire a notification to the watch associated with this dispatcher,
  // provided we're armed and it cares about the new state.
  if (it->second->NotifyState(state, armed_)) {
    ready_watches_.insert(it->second.get());

    // If we were armed and got here, we notified the watch. Disarm.
    armed_ = false;
  } else {
    ready_watches_.erase(it->second.get());
  }
}

void WatcherDispatcher::NotifyHandleClosed(Dispatcher* dispatcher) {
  scoped_refptr<Watch> watch;
  {
    base::AutoLock lock(lock_);
    auto it = watched_handles_.find(dispatcher);
    if (it == watched_handles_.end())
      return;

    watch = std::move(it->second);

    // TODO(crbug.com/740044): Remove this CHECK.
    CHECK(watch);

    // Wipe out all state associated with the closed dispatcher.
    watches_.erase(watch->context());
    ready_watches_.erase(watch.get());
    watched_handles_.erase(it);
  }

  // NOTE: It's important that this is called outside of |lock_| since it
  // acquires internal Watch locks.
  watch->Cancel();
}

void WatcherDispatcher::InvokeWatchCallback(
    uintptr_t context,
    MojoResult result,
    const HandleSignalsState& state,
    MojoWatcherNotificationFlags flags) {
  {
    // We avoid holding the lock during dispatch. It's OK for notification
    // callbacks to close this watcher, and it's OK for notifications to race
    // with closure, if for example the watcher is closed from another thread
    // between this test and the invocation of |callback_| below.
    //
    // Because cancellation synchronously blocks all future notifications, and
    // because notifications themselves are mutually exclusive for any given
    // context, we still guarantee that a single MOJO_RESULT_CANCELLED result
    // is the last notification received for any given context.
    //
    // This guarantee is sufficient to make safe, synchronized, per-context
    // state management possible in user code.
    base::AutoLock lock(lock_);
    if (closed_ && result != MOJO_RESULT_CANCELLED)
      return;
  }

  callback_(context, result, static_cast<MojoHandleSignalsState>(state), flags);
}

Dispatcher::Type WatcherDispatcher::GetType() const {
  return Type::WATCHER;
}

MojoResult WatcherDispatcher::Close() {
  // We swap out all the watched handle information onto the stack so we can
  // call into their dispatchers without our own lock held.
  std::map<uintptr_t, scoped_refptr<Watch>> watches;
  {
    base::AutoLock lock(lock_);
    DCHECK(!closed_);
    closed_ = true;
    std::swap(watches, watches_);
    watched_handles_.clear();
  }

  // Remove all refs from our watched dispatchers and fire cancellations.
  for (auto& entry : watches) {
    entry.second->dispatcher()->RemoveWatcherRef(this, entry.first);
    entry.second->Cancel();
  }

  return MOJO_RESULT_OK;
}

MojoResult WatcherDispatcher::WatchDispatcher(
    scoped_refptr<Dispatcher> dispatcher,
    MojoHandleSignals signals,
    MojoWatchCondition condition,
    uintptr_t context) {
  // NOTE: Because it's critical to avoid acquiring any other dispatcher locks
  // while |lock_| is held, we defer adding oursevles to the dispatcher until
  // after we've updated all our own relevant state and released |lock_|.
  {
    base::AutoLock lock(lock_);

    // TODO(crbug.com/740044): Remove this CHECK.
    CHECK(!closed_);

    if (watches_.count(context) || watched_handles_.count(dispatcher.get()))
      return MOJO_RESULT_ALREADY_EXISTS;

    scoped_refptr<Watch> watch =
        new Watch(this, dispatcher, context, signals, condition);
    watches_.insert({context, watch});
    auto result =
        watched_handles_.insert(std::make_pair(dispatcher.get(), watch));
    DCHECK(result.second);
  }

  MojoResult rv = dispatcher->AddWatcherRef(this, context);
  if (rv != MOJO_RESULT_OK) {
    // Oops. This was not a valid handle to watch. Undo the above work and
    // fail gracefully.
    base::AutoLock lock(lock_);
    watches_.erase(context);
    watched_handles_.erase(dispatcher.get());
    return rv;
  }

  return MOJO_RESULT_OK;
}

MojoResult WatcherDispatcher::CancelWatch(uintptr_t context) {
  // We may remove the last stored ref to the Watch below, so we retain
  // a reference on the stack.
  scoped_refptr<Watch> watch;
  {
    base::AutoLock lock(lock_);
    auto it = watches_.find(context);
    if (it == watches_.end())
      return MOJO_RESULT_NOT_FOUND;
    watch = it->second;
    watches_.erase(it);
  }

  // TODO(crbug.com/740044): Remove these CHECKs.
  CHECK(watch);
  CHECK(watch->dispatcher());
  CHECK(this);

  // Mark the watch as cancelled so no further notifications get through.
  watch->Cancel();

  // We remove the watcher ref for this context before updating any more
  // internal watcher state, ensuring that we don't receiving further
  // notifications for this context.
  watch->dispatcher()->RemoveWatcherRef(this, context);

  {
    base::AutoLock lock(lock_);
    auto handle_it = watched_handles_.find(watch->dispatcher().get());

    // If another thread races to close this watcher handler, |watched_handles_|
    // may have been cleared by the time we reach this section.
    if (handle_it == watched_handles_.end())
      return MOJO_RESULT_OK;

    ready_watches_.erase(handle_it->second.get());
    watched_handles_.erase(handle_it);
  }

  return MOJO_RESULT_OK;
}

MojoResult WatcherDispatcher::Arm(
    uint32_t* num_ready_contexts,
    uintptr_t* ready_contexts,
    MojoResult* ready_results,
    MojoHandleSignalsState* ready_signals_states) {
  base::AutoLock lock(lock_);
  if (num_ready_contexts &&
      (!ready_contexts || !ready_results || !ready_signals_states)) {
    return MOJO_RESULT_INVALID_ARGUMENT;
  }

  if (watched_handles_.empty())
    return MOJO_RESULT_NOT_FOUND;

  if (ready_watches_.empty()) {
    // Fast path: No watches are ready to notify, so we're done.
    armed_ = true;
    return MOJO_RESULT_OK;
  }

  if (num_ready_contexts) {
    DCHECK_LE(ready_watches_.size(), std::numeric_limits<uint32_t>::max());
    *num_ready_contexts = std::min(
        *num_ready_contexts, static_cast<uint32_t>(ready_watches_.size()));

    WatchSet::const_iterator next_ready_iter = ready_watches_.begin();
    if (last_watch_to_block_arming_) {
      // Find the next watch to notify in simple round-robin order on the
      // |ready_watches_| map, wrapping around to the beginning if necessary.
      next_ready_iter = ready_watches_.find(last_watch_to_block_arming_);
      if (next_ready_iter != ready_watches_.end())
        ++next_ready_iter;
      if (next_ready_iter == ready_watches_.end())
        next_ready_iter = ready_watches_.begin();
    }

    for (size_t i = 0; i < *num_ready_contexts; ++i) {
      const Watch* const watch = *next_ready_iter;
      ready_contexts[i] = watch->context();
      ready_results[i] = watch->last_known_result();
      ready_signals_states[i] = watch->last_known_signals_state();

      // Iterate and wrap around.
      last_watch_to_block_arming_ = watch;
      ++next_ready_iter;
      if (next_ready_iter == ready_watches_.end())
        next_ready_iter = ready_watches_.begin();
    }
  }

  return MOJO_RESULT_FAILED_PRECONDITION;
}

WatcherDispatcher::~WatcherDispatcher() {
  // TODO(crbug.com/740044): Remove this.
  CHECK(closed_);
}

}  // namespace edk
}  // namespace mojo
