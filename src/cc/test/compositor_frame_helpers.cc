// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cc/test/compositor_frame_helpers.h"

#include "cc/output/compositor_frame.h"

namespace cc {
namespace test {

CompositorFrame MakeCompositorFrame() {
  CompositorFrame frame = MakeEmptyCompositorFrame();
  std::unique_ptr<RenderPass> pass = RenderPass::Create();
  pass->SetNew(1, gfx::Rect(0, 0, 20, 20), gfx::Rect(), gfx::Transform());
  frame.render_pass_list.push_back(std::move(pass));
  return frame;
}

CompositorFrame MakeEmptyCompositorFrame() {
  CompositorFrame frame;
  frame.metadata.begin_frame_ack.source_id =
      viz::BeginFrameArgs::kManualSourceId;
  frame.metadata.begin_frame_ack.sequence_number =
      viz::BeginFrameArgs::kStartingFrameNumber;
  frame.metadata.begin_frame_ack.has_damage = true;
  frame.metadata.device_scale_factor = 1;
  return frame;
}

CompositorFrame MakeCompositorFrame(
    std::vector<viz::SurfaceId> activation_dependencies,
    std::vector<viz::SurfaceId> referenced_surfaces,
    std::vector<TransferableResource> resource_list) {
  CompositorFrame compositor_frame = test::MakeCompositorFrame();
  compositor_frame.metadata.begin_frame_ack = viz::BeginFrameAck(0, 1, true);
  compositor_frame.metadata.activation_dependencies =
      std::move(activation_dependencies);
  compositor_frame.metadata.referenced_surfaces =
      std::move(referenced_surfaces);
  compositor_frame.resource_list = std::move(resource_list);
  return compositor_frame;
}

}  // namespace test
}  // namespace cc
