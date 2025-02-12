//
//  JointState.cpp
//  libraries/animation/src/
//
//  Created by Andrzej Kapolka on 10/18/13.
//  Copyright 2013 High Fidelity, Inc.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#include <glm/gtx/norm.hpp>

#include <QThreadPool>

#include <AngularConstraint.h>
#include <SharedUtil.h>

#include "JointState.h"

JointState::~JointState() {
    if (_constraint) {
        delete _constraint;
        _constraint = NULL;
    }
}

void JointState::copyState(const JointState& other) {
    _transformChanged = other._transformChanged;
    _transform = other._transform;
    _rotationIsValid = other._rotationIsValid;
    _rotation = other._rotation;
    _rotationInConstrainedFrame = other._rotationInConstrainedFrame;
    _positionInParentFrame = other._positionInParentFrame;
    _distanceToParent = other._distanceToParent;
    _animationPriority = other._animationPriority;
    
    _visibleTransform = other._visibleTransform;
    _visibleRotation = extractRotation(_visibleTransform);
    _visibleRotationInConstrainedFrame = other._visibleRotationInConstrainedFrame;
    // DO NOT copy _constraint
    _name = other._name;
    _isFree = other._isFree;
    _boneRadius = other._boneRadius;
    _parentIndex = other._parentIndex;
    _defaultRotation = other._defaultRotation;
    _inverseDefaultRotation = other._inverseDefaultRotation;
    _translation = other._translation;
    _rotationMin = other._rotationMin;
    _rotationMax = other._rotationMax;
    _preRotation = other._preRotation;
    _postRotation = other._postRotation;
    _preTransform = other._preTransform;
    _postTransform = other._postTransform;
    _inverseBindRotation = other._inverseBindRotation;
}
JointState::JointState(const FBXJoint& joint) {
    _rotationInConstrainedFrame = joint.rotation;
    _name = joint.name;
    _isFree = joint.isFree;
    _boneRadius = joint.boneRadius;
    _parentIndex = joint.parentIndex;
    _translation = joint.translation;
    _defaultRotation = joint.rotation;
    _inverseDefaultRotation = joint.inverseDefaultRotation;
    _rotationMin = joint.rotationMin;
    _rotationMax = joint.rotationMax;
    _preRotation = joint.preRotation;
    _postRotation = joint.postRotation;
    _preTransform = joint.preTransform;
    _postTransform = joint.postTransform;
    _inverseBindRotation = joint.inverseBindRotation;
}

void JointState::buildConstraint() {
    if (_constraint) {
        delete _constraint;
        _constraint = NULL;
    }
    if (glm::distance2(glm::vec3(-PI), _rotationMin) > EPSILON ||
            glm::distance2(glm::vec3(PI), _rotationMax) > EPSILON ) {
        // this joint has rotation constraints
        _constraint = AngularConstraint::newAngularConstraint(_rotationMin, _rotationMax);
    }
}

glm::quat JointState::getRotation() const {
    if (!_rotationIsValid) {
        const_cast<JointState*>(this)->_rotation = extractRotation(_transform);
        const_cast<JointState*>(this)->_rotationIsValid = true;
    }
    
    return _rotation;
}

void JointState::initTransform(const glm::mat4& parentTransform) {
    computeTransform(parentTransform);
    _positionInParentFrame = glm::inverse(extractRotation(parentTransform)) * (extractTranslation(_transform) - extractTranslation(parentTransform));
    _distanceToParent = glm::length(_positionInParentFrame);
}

void JointState::computeTransform(const glm::mat4& parentTransform, bool parentTransformChanged, bool synchronousRotationCompute) {
    if (!parentTransformChanged && !_transformChanged) {
        return;
    }
    
    glm::quat rotationInParentFrame = _preRotation * _rotationInConstrainedFrame * _postRotation;
    glm::mat4 transformInParentFrame = _preTransform * glm::mat4_cast(rotationInParentFrame) * _postTransform;
    glm::mat4 newTransform = parentTransform * glm::translate(_translation) * transformInParentFrame;
    
    if (newTransform != _transform) {
        _transform = newTransform;
        _transformChanged = true;
        _rotationIsValid = false;
    }
}

void JointState::computeVisibleTransform(const glm::mat4& parentTransform) {
    glm::quat rotationInParentFrame = _preRotation * _visibleRotationInConstrainedFrame * _postRotation;
    glm::mat4 transformInParentFrame = _preTransform * glm::mat4_cast(rotationInParentFrame) * _postTransform;
    _visibleTransform = parentTransform * glm::translate(_translation) * transformInParentFrame;
    _visibleRotation = extractRotation(_visibleTransform);
}

glm::quat JointState::getRotationInBindFrame() const {
    return getRotation() * _inverseBindRotation;
}

glm::quat JointState::getRotationInParentFrame() const {
    return _preRotation * _rotationInConstrainedFrame * _postRotation;
}

glm::quat JointState::getVisibleRotationInParentFrame() const {
    return _preRotation * _visibleRotationInConstrainedFrame * _postRotation;
}

void JointState::restoreRotation(float fraction, float priority) {
    if (priority == _animationPriority || _animationPriority == 0.0f) {
        setRotationInConstrainedFrameInternal(safeMix(_rotationInConstrainedFrame, _defaultRotation, fraction));
        _animationPriority = 0.0f;
    }
}

void JointState::setRotationInBindFrame(const glm::quat& rotation, float priority, bool constrain) {
    // rotation is from bind- to model-frame
    if (priority >= _animationPriority) {
        glm::quat targetRotation = _rotationInConstrainedFrame * glm::inverse(getRotation()) * rotation * glm::inverse(_inverseBindRotation);
        if (constrain && _constraint) {
            _constraint->softClamp(targetRotation, _rotationInConstrainedFrame, 0.5f);
        }
        setRotationInConstrainedFrameInternal(targetRotation);
        _animationPriority = priority;
    }
}

void JointState::setRotationInModelFrame(const glm::quat& rotationInModelFrame, float priority, bool constrain) {
    // rotation is from bind- to model-frame
    if (priority >= _animationPriority) {
        glm::quat parentRotation = computeParentRotation();

        // R = Rp * Rpre * r * Rpost
        // R' = Rp * Rpre * r' * Rpost
        // r' = (Rp * Rpre)^ * R' * Rpost^
        glm::quat targetRotation = glm::inverse(parentRotation * _preRotation) * rotationInModelFrame * glm::inverse(_postRotation);
        if (constrain && _constraint) {
            _constraint->softClamp(targetRotation, _rotationInConstrainedFrame, 0.5f);
        }
        _rotationInConstrainedFrame = glm::normalize(targetRotation);
        _transformChanged = true;
        _animationPriority = priority;
    }
}

void JointState::clearTransformTranslation() {
    _transform[3][0] = 0.0f;
    _transform[3][1] = 0.0f;
    _transform[3][2] = 0.0f;
    _transformChanged = true;
    _visibleTransform[3][0] = 0.0f;
    _visibleTransform[3][1] = 0.0f;
    _visibleTransform[3][2] = 0.0f;
}

void JointState::applyRotationDelta(const glm::quat& delta, bool constrain, float priority) {
    // NOTE: delta is in model-frame
    if (priority < _animationPriority || delta == glm::quat()) {
        return;
    }
    _animationPriority = priority;
    glm::quat targetRotation = _rotationInConstrainedFrame * glm::inverse(getRotation()) * delta * getRotation();
    if (!constrain || _constraint == NULL) {
        // no constraints
        _rotationInConstrainedFrame = targetRotation;
        _transformChanged = true;
        
        _rotation = delta * getRotation();
        return;
    }
    setRotationInConstrainedFrameInternal(targetRotation);
}

/// Applies delta rotation to joint but mixes a little bit of the default pose as well.
/// This helps keep an IK solution stable.
void JointState::mixRotationDelta(const glm::quat& delta, float mixFactor, float priority) {
    // NOTE: delta is in model-frame
    if (priority < _animationPriority) {
        return;
    }
    _animationPriority = priority;
    glm::quat targetRotation = _rotationInConstrainedFrame * glm::inverse(getRotation()) * delta * getRotation();
    if (mixFactor > 0.0f && mixFactor <= 1.0f) {
        targetRotation = safeMix(targetRotation, _defaultRotation, mixFactor);
    }
    if (_constraint) {
        _constraint->softClamp(targetRotation, _rotationInConstrainedFrame, 0.5f);
    }
    setRotationInConstrainedFrameInternal(targetRotation);
}

void JointState::mixVisibleRotationDelta(const glm::quat& delta, float mixFactor) {
    // NOTE: delta is in model-frame
    glm::quat targetRotation = _visibleRotationInConstrainedFrame * glm::inverse(_visibleRotation) * delta * _visibleRotation;
    if (mixFactor > 0.0f && mixFactor <= 1.0f) {
        targetRotation = safeMix(targetRotation, _rotationInConstrainedFrame, mixFactor);
    }
    setVisibleRotationInConstrainedFrame(targetRotation);
}

glm::quat JointState::computeParentRotation() const {
    // R = Rp * Rpre * r * Rpost
    // Rp = R * (Rpre * r * Rpost)^
    return getRotation() * glm::inverse(_preRotation * _rotationInConstrainedFrame * _postRotation);
}

glm::quat JointState::computeVisibleParentRotation() const {
    return _visibleRotation * glm::inverse(_preRotation * _visibleRotationInConstrainedFrame * _postRotation);
}

void JointState::setRotationInConstrainedFrame(glm::quat targetRotation, float priority, bool constrain, float mix) {
    if (priority >= _animationPriority || _animationPriority == 0.0f) {
        if (constrain && _constraint) {
            _constraint->softClamp(targetRotation, _rotationInConstrainedFrame, 0.5f);
        }
        auto rotation = (mix == 1.0f) ? targetRotation : safeMix(getRotationInConstrainedFrame(), targetRotation, mix);
        setRotationInConstrainedFrameInternal(rotation);
        _animationPriority = priority;
    }
}

void JointState::setRotationInConstrainedFrameInternal(const glm::quat& targetRotation) {
    if (_rotationInConstrainedFrame != targetRotation) {
        glm::quat parentRotation = computeParentRotation();
        _rotationInConstrainedFrame = targetRotation;
        _transformChanged = true;
        // R' = Rp * Rpre * r' * Rpost
        _rotation = parentRotation * _preRotation * _rotationInConstrainedFrame * _postRotation;
    }
}

void JointState::setVisibleRotationInConstrainedFrame(const glm::quat& targetRotation) {
    glm::quat parentRotation = computeVisibleParentRotation();
    _visibleRotationInConstrainedFrame = targetRotation;
    _visibleRotation = parentRotation * _preRotation * _visibleRotationInConstrainedFrame * _postRotation;
}

bool JointState::rotationIsDefault(const glm::quat& rotation, float tolerance) const {
    glm::quat defaultRotation = _defaultRotation;
    return glm::abs(rotation.x - defaultRotation.x) < tolerance &&
        glm::abs(rotation.y - defaultRotation.y) < tolerance &&
        glm::abs(rotation.z - defaultRotation.z) < tolerance &&
        glm::abs(rotation.w - defaultRotation.w) < tolerance;
}

glm::quat JointState::getDefaultRotationInParentFrame() const {
    // NOTE: the result is constant and could be cached
    return _preRotation * _defaultRotation * _postRotation;
}

const glm::vec3& JointState::getDefaultTranslationInConstrainedFrame() const {
    return _translation;
}

void JointState::slaveVisibleTransform() {
    _visibleTransform = _transform;
    _visibleRotation = getRotation();
    _visibleRotationInConstrainedFrame = _rotationInConstrainedFrame;
}
