//
//  AnimDebugDraw.h
//
//  Copyright 2015 High Fidelity, Inc.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#ifndef hifi_AnimDebugDraw_h
#define hifi_AnimDebugDraw_h

#include <tuple>

#include "render/Scene.h"
#include "gpu/Pipeline.h"
#include "AnimNode.h"
#include "AnimSkeleton.h"

class AnimDebugDrawData;
typedef render::Payload<AnimDebugDrawData> AnimDebugDrawPayload;

class AnimDebugDraw {
public:
    static AnimDebugDraw& getInstance();

    AnimDebugDraw();
    ~AnimDebugDraw();

    void addSkeleton(std::string key, AnimSkeleton::ConstPointer skeleton, const AnimPose& rootPose, const glm::vec4& color);
    void removeSkeleton(std::string key);

    void addAnimNode(std::string key, AnimNode::ConstPointer animNode, const AnimPose& rootPose, const glm::vec4& color);
    void removeAnimNode(std::string key);

    void update();

protected:
    std::shared_ptr<AnimDebugDrawData> _animDebugDrawData;
    std::shared_ptr<AnimDebugDrawPayload> _animDebugDrawPayload;
    render::ItemID _itemID;

    static gpu::PipelinePointer _pipeline;

    typedef std::tuple<AnimSkeleton::ConstPointer, AnimPose, glm::vec4> SkeletonInfo;
    typedef std::tuple<AnimNode::ConstPointer, AnimPose, glm::vec4> AnimNodeInfo;

    std::unordered_map<std::string, SkeletonInfo> _skeletons;
    std::unordered_map<std::string, AnimNodeInfo> _animNodes;

    // no copies
    AnimDebugDraw(const AnimDebugDraw&) = delete;
    AnimDebugDraw& operator=(const AnimDebugDraw&) = delete;
};

#endif // hifi_AnimDebugDraw
