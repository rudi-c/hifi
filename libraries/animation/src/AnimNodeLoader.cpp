//
//  AnimNodeLoader.cpp
//
//  Created by Anthony J. Thibault on 9/2/15.
//  Copyright (c) 2015 High Fidelity, Inc. All rights reserved.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFile>

#include "AnimNode.h"
#include "AnimClip.h"
#include "AnimBlendLinear.h"
#include "AnimationLogging.h"
#include "AnimOverlay.h"
#include "AnimNodeLoader.h"
#include "AnimStateMachine.h"

using NodeLoaderFunc = AnimNode::Pointer (*)(const QJsonObject& jsonObj, const QString& id, const QUrl& jsonUrl);
using NodeProcessFunc = bool (*)(AnimNode::Pointer node, const QJsonObject& jsonObj, const QString& id, const QUrl& jsonUrl);

// factory functions
static AnimNode::Pointer loadClipNode(const QJsonObject& jsonObj, const QString& id, const QUrl& jsonUrl);
static AnimNode::Pointer loadBlendLinearNode(const QJsonObject& jsonObj, const QString& id, const QUrl& jsonUrl);
static AnimNode::Pointer loadOverlayNode(const QJsonObject& jsonObj, const QString& id, const QUrl& jsonUrl);
static AnimNode::Pointer loadStateMachineNode(const QJsonObject& jsonObj, const QString& id, const QUrl& jsonUrl);

// called after children have been loaded
static bool processClipNode(AnimNode::Pointer node, const QJsonObject& jsonObj, const QString& id, const QUrl& jsonUrl) { return true; }
static bool processBlendLinearNode(AnimNode::Pointer node, const QJsonObject& jsonObj, const QString& id, const QUrl& jsonUrl) { return true; }
static bool processOverlayNode(AnimNode::Pointer node, const QJsonObject& jsonObj, const QString& id, const QUrl& jsonUrl) { return true; }
bool processStateMachineNode(AnimNode::Pointer node, const QJsonObject& jsonObj, const QString& id, const QUrl& jsonUrl);

static const char* animNodeTypeToString(AnimNode::Type type) {
    switch (type) {
    case AnimNode::Type::Clip: return "clip";
    case AnimNode::Type::BlendLinear: return "blendLinear";
    case AnimNode::Type::Overlay: return "overlay";
    case AnimNode::Type::StateMachine: return "stateMachine";
    case AnimNode::Type::NumTypes: return nullptr;
    };
    return nullptr;
}

static NodeLoaderFunc animNodeTypeToLoaderFunc(AnimNode::Type type) {
    switch (type) {
    case AnimNode::Type::Clip: return loadClipNode;
    case AnimNode::Type::BlendLinear: return loadBlendLinearNode;
    case AnimNode::Type::Overlay: return loadOverlayNode;
    case AnimNode::Type::StateMachine: return loadStateMachineNode;
    case AnimNode::Type::NumTypes: return nullptr;
    };
    return nullptr;
}

static NodeProcessFunc animNodeTypeToProcessFunc(AnimNode::Type type) {
    switch (type) {
    case AnimNode::Type::Clip: return processClipNode;
    case AnimNode::Type::BlendLinear: return processBlendLinearNode;
    case AnimNode::Type::Overlay: return processOverlayNode;
    case AnimNode::Type::StateMachine: return processStateMachineNode;
    case AnimNode::Type::NumTypes: return nullptr;
    };
    return nullptr;
}

#define READ_STRING(NAME, JSON_OBJ, ID, URL)                            \
    auto NAME##_VAL = JSON_OBJ.value(#NAME);                            \
    if (!NAME##_VAL.isString()) {                                       \
        qCCritical(animation) << "AnimNodeLoader, error reading string" \
                              << #NAME << ", id =" << ID                \
                              << ", url =" << URL.toDisplayString();    \
        return nullptr;                                                 \
    }                                                                   \
    QString NAME = NAME##_VAL.toString()

#define READ_OPTIONAL_STRING(NAME, JSON_OBJ)                            \
    auto NAME##_VAL = JSON_OBJ.value(#NAME);                            \
    QString NAME;                                                       \
    if (NAME##_VAL.isString()) {                                        \
        NAME = NAME##_VAL.toString();                                   \
    }

#define READ_BOOL(NAME, JSON_OBJ, ID, URL)                              \
    auto NAME##_VAL = JSON_OBJ.value(#NAME);                            \
    if (!NAME##_VAL.isBool()) {                                         \
        qCCritical(animation) << "AnimNodeLoader, error reading bool"   \
                              << #NAME << ", id =" << ID                \
                              << ", url =" << URL.toDisplayString();    \
        return nullptr;                                                 \
    }                                                                   \
    bool NAME = NAME##_VAL.toBool()

#define READ_FLOAT(NAME, JSON_OBJ, ID, URL)                             \
    auto NAME##_VAL = JSON_OBJ.value(#NAME);                            \
    if (!NAME##_VAL.isDouble()) {                                       \
        qCCritical(animation) << "AnimNodeLoader, error reading double" \
                              << #NAME << "id =" << ID                  \
                              << ", url =" << URL.toDisplayString();    \
        return nullptr;                                                 \
    }                                                                   \
    float NAME = (float)NAME##_VAL.toDouble()

static AnimNode::Type stringToEnum(const QString& str) {
    // O(n), move to map when number of types becomes large.
    const int NUM_TYPES = static_cast<int>(AnimNode::Type::NumTypes);
    for (int i = 0; i < NUM_TYPES; i++ ) {
        AnimNode::Type type = static_cast<AnimNode::Type>(i);
        if (str == animNodeTypeToString(type)) {
            return type;
        }
    }
    return AnimNode::Type::NumTypes;
}

static AnimNode::Pointer loadNode(const QJsonObject& jsonObj, const QUrl& jsonUrl) {
    auto idVal = jsonObj.value("id");
    if (!idVal.isString()) {
        qCCritical(animation) << "AnimNodeLoader, bad string \"id\", url =" << jsonUrl.toDisplayString();
        return nullptr;
    }
    QString id = idVal.toString();

    auto typeVal = jsonObj.value("type");
    if (!typeVal.isString()) {
        qCCritical(animation) << "AnimNodeLoader, bad object \"type\", id =" << id << ", url =" << jsonUrl.toDisplayString();
        return nullptr;
    }
    QString typeStr = typeVal.toString();
    AnimNode::Type type = stringToEnum(typeStr);
    if (type == AnimNode::Type::NumTypes) {
        qCCritical(animation) << "AnimNodeLoader, unknown node type" << typeStr << ", id =" << id << ", url =" << jsonUrl.toDisplayString();
        return nullptr;
    }

    auto dataValue = jsonObj.value("data");
    if (!dataValue.isObject()) {
        qCCritical(animation) << "AnimNodeLoader, bad string \"data\", id =" << id << ", url =" << jsonUrl.toDisplayString();
        return nullptr;
    }
    auto dataObj = dataValue.toObject();

    assert((int)type >= 0 && type < AnimNode::Type::NumTypes);
    auto node = (animNodeTypeToLoaderFunc(type))(dataObj, id, jsonUrl);

    auto childrenValue = jsonObj.value("children");
    if (!childrenValue.isArray()) {
        qCCritical(animation) << "AnimNodeLoader, bad array \"children\", id =" << id << ", url =" << jsonUrl.toDisplayString();
        return nullptr;
    }
    auto childrenArray = childrenValue.toArray();
    for (const auto& childValue : childrenArray) {
        if (!childValue.isObject()) {
            qCCritical(animation) << "AnimNodeLoader, bad object in \"children\", id =" << id << ", url =" << jsonUrl.toDisplayString();
            return nullptr;
        }
        node->addChild(loadNode(childValue.toObject(), jsonUrl));
    }

    if ((animNodeTypeToProcessFunc(type))(node, dataObj, id, jsonUrl)) {
        return node;
    } else {
        return nullptr;
    }
}

static AnimNode::Pointer loadClipNode(const QJsonObject& jsonObj, const QString& id, const QUrl& jsonUrl) {

    READ_STRING(url, jsonObj, id, jsonUrl);
    READ_FLOAT(startFrame, jsonObj, id, jsonUrl);
    READ_FLOAT(endFrame, jsonObj, id, jsonUrl);
    READ_FLOAT(timeScale, jsonObj, id, jsonUrl);
    READ_BOOL(loopFlag, jsonObj, id, jsonUrl);

    READ_OPTIONAL_STRING(startFrameVar, jsonObj);
    READ_OPTIONAL_STRING(endFrameVar, jsonObj);
    READ_OPTIONAL_STRING(timeScaleVar, jsonObj);
    READ_OPTIONAL_STRING(loopFlagVar, jsonObj);

    auto node = std::make_shared<AnimClip>(id.toStdString(), url.toStdString(), startFrame, endFrame, timeScale, loopFlag);

    if (!startFrameVar.isEmpty()) {
        node->setStartFrameVar(startFrameVar.toStdString());
    }
    if (!endFrameVar.isEmpty()) {
        node->setEndFrameVar(endFrameVar.toStdString());
    }
    if (!timeScaleVar.isEmpty()) {
        node->setTimeScaleVar(timeScaleVar.toStdString());
    }
    if (!loopFlagVar.isEmpty()) {
        node->setLoopFlagVar(loopFlagVar.toStdString());
    }

    return node;
}

static AnimNode::Pointer loadBlendLinearNode(const QJsonObject& jsonObj, const QString& id, const QUrl& jsonUrl) {

    READ_FLOAT(alpha, jsonObj, id, jsonUrl);

    READ_OPTIONAL_STRING(alphaVar, jsonObj);

    auto node = std::make_shared<AnimBlendLinear>(id.toStdString(), alpha);

    if (!alphaVar.isEmpty()) {
        node->setAlphaVar(alphaVar.toStdString());
    }

    return node;
}

static const char* boneSetStrings[AnimOverlay::NumBoneSets] = {
    "fullBody",
    "upperBody",
    "lowerBody",
    "rightArm",
    "leftArm",
    "aboveTheHead",
    "belowTheHead",
    "headOnly",
    "spineOnly",
    "empty"
};

static AnimOverlay::BoneSet stringToBoneSetEnum(const QString& str) {
    for (int i = 0; i < (int)AnimOverlay::NumBoneSets; i++) {
        if (str == boneSetStrings[i]) {
            return (AnimOverlay::BoneSet)i;
        }
    }
    return AnimOverlay::NumBoneSets;
}

static AnimNode::Pointer loadOverlayNode(const QJsonObject& jsonObj, const QString& id, const QUrl& jsonUrl) {

    READ_STRING(boneSet, jsonObj, id, jsonUrl);
    READ_FLOAT(alpha, jsonObj, id, jsonUrl);

    auto boneSetEnum = stringToBoneSetEnum(boneSet);
    if (boneSetEnum == AnimOverlay::NumBoneSets) {
        qCCritical(animation) << "AnimNodeLoader, unknown bone set =" << boneSet << ", defaulting to \"fullBody\", url =" << jsonUrl.toDisplayString();
        boneSetEnum = AnimOverlay::FullBodyBoneSet;
    }

    READ_OPTIONAL_STRING(boneSetVar, jsonObj);
    READ_OPTIONAL_STRING(alphaVar, jsonObj);

    auto node = std::make_shared<AnimOverlay>(id.toStdString(), boneSetEnum, alpha);

    if (!boneSetVar.isEmpty()) {
        node->setBoneSetVar(boneSetVar.toStdString());
    }
    if (!alphaVar.isEmpty()) {
        node->setAlphaVar(alphaVar.toStdString());
    }

    return node;
}

static AnimNode::Pointer loadStateMachineNode(const QJsonObject& jsonObj, const QString& id, const QUrl& jsonUrl) {
    auto node = std::make_shared<AnimStateMachine>(id.toStdString());
    return node;
}

void buildChildMap(std::map<std::string, AnimNode::Pointer>& map, AnimNode::Pointer node) {
    for ( auto child : node->_children ) {
        map.insert(std::pair<std::string, AnimNode::Pointer>(child->_id, child));
    }
}

bool processStateMachineNode(AnimNode::Pointer node, const QJsonObject& jsonObj, const QString& nodeId, const QUrl& jsonUrl) {
    auto smNode = std::static_pointer_cast<AnimStateMachine>(node);
    assert(smNode);

    READ_STRING(currentState, jsonObj, nodeId, jsonUrl);

    auto statesValue = jsonObj.value("states");
    if (!statesValue.isArray()) {
        qCCritical(animation) << "AnimNodeLoader, bad array \"states\" in stateMachine node, id =" << nodeId << ", url =" << jsonUrl.toDisplayString();
        return nullptr;
    }

    // build a map for all children by name.
    std::map<std::string, AnimNode::Pointer> childMap;
    buildChildMap(childMap, node);

    // first pass parse all the states and build up the state and transition map.
    using StringPair = std::pair<std::string, std::string>;
    using TransitionMap = std::multimap<AnimStateMachine::State::Pointer, StringPair>;
    TransitionMap transitionMap;

    using StateMap = std::map<std::string, AnimStateMachine::State::Pointer>;
    StateMap stateMap;

    auto statesArray = statesValue.toArray();
    for (const auto& stateValue : statesArray) {
        if (!stateValue.isObject()) {
            qCCritical(animation) << "AnimNodeLoader, bad state object in \"states\", id =" << nodeId << ", url =" << jsonUrl.toDisplayString();
            return nullptr;
        }
        auto stateObj = stateValue.toObject();

        READ_STRING(id, stateObj, nodeId, jsonUrl);
        READ_FLOAT(interpTarget, stateObj, nodeId, jsonUrl);
        READ_FLOAT(interpDuration, stateObj, nodeId, jsonUrl);

        READ_OPTIONAL_STRING(interpTargetVar, stateObj);
        READ_OPTIONAL_STRING(interpDurationVar, stateObj);

        auto stdId = id.toStdString();

        auto iter = childMap.find(stdId);
        if (iter == childMap.end()) {
            qCCritical(animation) << "AnimNodeLoader, could not find stateMachine child (state) with nodeId =" << nodeId << "stateId =" << id << "url =" << jsonUrl.toDisplayString();
            return nullptr;
        }

        auto statePtr = std::make_shared<AnimStateMachine::State>(stdId, iter->second, interpTarget, interpDuration);
        assert(statePtr);

        if (!interpTargetVar.isEmpty()) {
            statePtr->setInterpTargetVar(interpTargetVar.toStdString());
        }
        if (!interpDurationVar.isEmpty()) {
            statePtr->setInterpDurationVar(interpDurationVar.toStdString());
        }

        smNode->addState(statePtr);
        stateMap.insert(StateMap::value_type(statePtr->getID(), statePtr));

        auto transitionsValue = stateObj.value("transitions");
        if (!transitionsValue.isArray()) {
            qCritical(animation) << "AnimNodeLoader, bad array \"transitions\" in stateMachine node, stateId =" << id << "nodeId =" << nodeId << "url =" << jsonUrl.toDisplayString();
            return nullptr;
        }

        auto transitionsArray = transitionsValue.toArray();
        for (const auto& transitionValue : transitionsArray) {
            if (!transitionValue.isObject()) {
                qCritical(animation) << "AnimNodeLoader, bad transition object in \"transtions\", stateId =" << id << "nodeId =" << nodeId << "url =" << jsonUrl.toDisplayString();
                return nullptr;
            }
            auto transitionObj = transitionValue.toObject();

            READ_STRING(var, transitionObj, nodeId, jsonUrl);
            READ_STRING(state, transitionObj, nodeId, jsonUrl);

            transitionMap.insert(TransitionMap::value_type(statePtr, StringPair(var.toStdString(), state.toStdString())));
        }
    }

    // second pass: now iterate thru all transitions and add them to the appropriate states.
    for (auto& transition : transitionMap) {
        AnimStateMachine::State::Pointer srcState = transition.first;
        auto iter = stateMap.find(transition.second.second);
        if (iter != stateMap.end()) {
            srcState->addTransition(AnimStateMachine::State::Transition(transition.second.first, iter->second));
        } else {
            qCCritical(animation) << "AnimNodeLoader, bad state machine transtion from srcState =" << srcState->_id.c_str() << "dstState =" << transition.second.second.c_str() << "nodeId =" << nodeId << "url = " << jsonUrl.toDisplayString();
            return nullptr;
        }
    }

    auto iter = stateMap.find(currentState.toStdString());
    if (iter == stateMap.end()) {
        qCCritical(animation) << "AnimNodeLoader, bad currentState =" << currentState << "could not find child node" << "id =" << nodeId << "url = " << jsonUrl.toDisplayString();
    }
    smNode->setCurrentState(iter->second);

    return true;
}

AnimNodeLoader::AnimNodeLoader(const QUrl& url) :
    _url(url),
    _resource(nullptr) {

    _resource = new Resource(url);
    connect(_resource, SIGNAL(loaded(QNetworkReply&)), SLOT(onRequestDone(QNetworkReply&)));
    connect(_resource, SIGNAL(failed(QNetworkReply::NetworkError)), SLOT(onRequestError(QNetworkReply::NetworkError)));
}

AnimNode::Pointer AnimNodeLoader::load(const QByteArray& contents, const QUrl& jsonUrl) {

    // convert string into a json doc
    QJsonParseError error;
    auto doc = QJsonDocument::fromJson(contents, &error);
    if (error.error != QJsonParseError::NoError) {
        qCCritical(animation) << "AnimNodeLoader, failed to parse json, error =" << error.errorString() << ", url =" << jsonUrl.toDisplayString();
        return nullptr;
    }
    QJsonObject obj = doc.object();

    // version
    QJsonValue versionVal = obj.value("version");
    if (!versionVal.isString()) {
        qCCritical(animation) << "AnimNodeLoader, bad string \"version\", url =" << jsonUrl.toDisplayString();
        return nullptr;
    }
    QString version = versionVal.toString();

    // check version
    if (version != "1.0") {
        qCCritical(animation) << "AnimNodeLoader, bad version number" << version << "expected \"1.0\", url =" << jsonUrl.toDisplayString();
        return nullptr;
    }

    // root
    QJsonValue rootVal = obj.value("root");
    if (!rootVal.isObject()) {
        qCCritical(animation) << "AnimNodeLoader, bad object \"root\", url =" << jsonUrl.toDisplayString();
        return nullptr;
    }

    return loadNode(rootVal.toObject(), jsonUrl);
}

void AnimNodeLoader::onRequestDone(QNetworkReply& request) {
    auto node = load(request.readAll(), _url);
    if (node) {
        emit success(node);
    } else {
        emit error(0, "json parse error");
    }
}

void AnimNodeLoader::onRequestError(QNetworkReply::NetworkError netError) {
    emit error((int)netError, "Resource download error");
}
