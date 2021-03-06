/// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_UI_SCENE_MANAGER_RESOURCES_NODES_ENTITY_NODE_H_
#define GARNET_BIN_UI_SCENE_MANAGER_RESOURCES_NODES_ENTITY_NODE_H_

#include "garnet/bin/ui/scene_manager/resources/nodes/node.h"

namespace scene_manager {

class EntityNode final : public Node {
 public:
  static const ResourceTypeInfo kTypeInfo;

  EntityNode(Session* session, scenic::ResourceId node_id);

  void Accept(class ResourceVisitor* visitor) override;
};

}  // namespace scene_manager

#endif  // GARNET_BIN_UI_SCENE_MANAGER_RESOURCES_NODES_ENTITY_NODE_H_
