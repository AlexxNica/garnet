// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_UI_TESTS_MOCKS_MOCK_VIEW_CONTAINER_LISTENER_H_
#define LIB_UI_TESTS_MOCKS_MOCK_VIEW_CONTAINER_LISTENER_H_

#include "lib/ui/views/fidl/view_manager.fidl.h"
#include "lib/ui/views/fidl/views.fidl.h"
#include "lib/fxl/macros.h"

namespace mozart {
namespace test {

using OnMockChildAttachedCallback =
    std::function<void(uint32_t, mozart::ViewInfoPtr)>;
using OnMockChildUnavailable = std::function<void(uint32_t)>;

class MockViewContainerListener : public mozart::ViewContainerListener {
 public:
  MockViewContainerListener();
  MockViewContainerListener(
      const OnMockChildAttachedCallback& child_attached_callback,
      const OnMockChildUnavailable& child_unavailable_callback);
  ~MockViewContainerListener();

 private:
  void OnChildAttached(uint32_t child_key,
                       mozart::ViewInfoPtr child_view_info,
                       const OnChildAttachedCallback& callback) override;
  void OnChildUnavailable(uint32_t child_key,
                          const OnChildUnavailableCallback& callback) override;

  OnMockChildAttachedCallback child_attached_callback_;
  OnMockChildUnavailable child_unavailable_callback_;
  FXL_DISALLOW_COPY_AND_ASSIGN(MockViewContainerListener);
};

}  // namespace test
}  // namespace mozart

#endif  // LIB_UI_TESTS_MOCKS_MOCK_VIEW_CONTAINER_LISTENER_H_
