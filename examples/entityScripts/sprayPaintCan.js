//
//  sprayPaintCan.js
//  examples/entityScripts
//
//  Created by Eric Levin on 9/3/15
//  Copyright 2015 High Fidelity, Inc.
//
//  This is an example of an entity script for painting with a spraypaint can
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html

(function() {
    this.userData = {};

    var TIP_OFFSET_Z = 0.14;
    var TIP_OFFSET_Y = 0.04;

    var ZERO_VEC = {
        x: 0,
        y: 0,
        z: 0
    }

    var MAX_POINTS_PER_LINE = 40;
    var MIN_POINT_DISTANCE = 0.01;
    var STROKE_WIDTH = 0.02;

    var self = this;

    var stopSetting = JSON.stringify({
        running: false
    });
    var startSetting = JSON.stringify({
        running: true
    });

    this.getUserData = function() {
        if (this.properties.userData) {
            this.userData = JSON.parse(this.properties.userData);
        }
    }

    this.updateUserData = function() {
        Entities.editEntity(this.entityId, {
            userData: JSON.stringify(this.userData)
        });
    }

    this.update = function(deltaTime) {
        self.properties = Entities.getEntityProperties(self.entityId);
        self.getUserData();
        //Only run the logic if this is the client whose avatar is grabbing
        if (self.userData.grabKey && self.userData.grabKey.activated === true && self.userData.grabKey.avatarId === MyAvatar.sessionUUID) {
            if (self.activated !== true) {
                Entities.editEntity(self.paintStream, {
                    animationSettings: startSetting
                });
                self.activated = true;
            }
            //Move emitter to where entity is always when its activated
            self.sprayStream();
        } else if (self.userData.grabKey && self.userData.grabKey.activated === false && self.activated) {
            Entities.editEntity(self.paintStream, {
                animationSettings: stopSetting
            });
            self.activated = false;
        }
    }

    this.sprayStream = function() {
        var forwardVec = Quat.getFront(self.properties.rotation);
        var upVec = Quat.getUp(self.properties.rotation);
        var position = Vec3.sum(self.properties.position, Vec3.multiply(forwardVec, TIP_OFFSET_Z));
        position = Vec3.sum(position, Vec3.multiply(upVec, TIP_OFFSET_Y))
        Entities.editEntity(self.paintStream, {
            position: position,
            emitVelocity: forwardVec
        });

        //Now check for an intersection with an entity

        //move forward so ray doesnt intersect with gun
        var origin = Vec3.sum(position, forwardVec);
        var pickRay = {
            origin: origin,
            direction: Vec3.multiply(forwardVec, 2)
        }

        var intersection = Entities.findRayIntersection(pickRay, true);
        if (intersection.intersects) {
            var normal = Vec3.multiply(-1, Quat.getFront(intersection.properties.rotation));
            this.paint(intersection.intersection, normal);
        }
    }

    this.paint = function(position, normal) {
        if (!this.painting) {
            print("position " + JSON.stringify(position))

            this.newStroke(position);
            this.painting = true;
        }

        if (this.strokePoints.length > MAX_POINTS_PER_LINE) {
            this.painting = false;
            return;
        }

        var localPoint = Vec3.subtract(position, this.strokeBasePosition);
        //Move stroke a bit forward along normal so it doesnt zfight with mesh its drawing on 
        localPoint = Vec3.sum(localPoint, Vec3.multiply(normal, .1));

        if (this.strokePoints.length > 0 && Vec3.distance(localPoint, this.strokePoints[this.strokePoints.length - 1]) < MIN_POINT_DISTANCE) {
            //need a minimum distance to avoid binormal NANs
            return;
        }

        this.strokePoints.push(localPoint);
        this.strokeNormals.push(normal);
        this.strokeWidths.push(STROKE_WIDTH);
        Entities.editEntity(this.currentStroke, {
            linePoints: this.strokePoints,
            normals: this.strokeNormals,
            strokeWidths: this.strokeWidths
        });

    }

    this.newStroke = function(position) {
        this.strokeBasePosition = position;
        this.currentStroke = Entities.addEntity({
            position: position,
            type: "PolyLine",
            color: {
                red: randInt(160, 250),
                green: randInt(10, 20),
                blue: randInt(190, 250)
            },
            dimensions: {
                x: 5,
                y: 5,
                z: 5
            },
            lifetime: 100
        });
        this.strokePoints = [];
        this.strokeNormals = [];
        this.strokeWidths = [];

        this.strokes.push(this.currentStroke);
    }

    this.preload = function(entityId) {
        this.strokes = [];
        this.activated = false;
        this.entityId = entityId;
        this.properties = Entities.getEntityProperties(self.entityId);
        this.getUserData();
        print("USER DATA " + JSON.stringify(this.userData))
        if (this.userData.activated) {
            this.activated = true;
        }
        this.initialize();
    }

    this.initialize = function() {
        var animationSettings = JSON.stringify({
            fps: 30,
            loop: true,
            firstFrame: 1,
            lastFrame: 10000,
            running: false
        });

        this.paintStream = Entities.addEntity({
            type: "ParticleEffect",
            animationSettings: animationSettings,
            position: this.properties.position,
            textures: "https://raw.githubusercontent.com/ericrius1/SantasLair/santa/assets/smokeparticle.png",
            emitVelocity: ZERO_VEC,
            emitAcceleration: ZERO_VEC,
            velocitySpread: {
                x: .02,
                y: .02,
                z: 0.02
            },
            emitRate: 100,
            particleRadius: 0.01,
            color: {
                red: 170,
                green: 20,
                blue: 150
            },
            lifespan: 5,
        });

    }

    this.unload = function() {
        Script.update.disconnect(this.update);
        Entities.deleteEntity(this.paintStream);
        this.strokes.forEach(function(stroke) {
            Entities.deleteEntity(stroke);
        });
    }
    Script.update.connect(this.update);
});


function randFloat(min, max) {
    return Math.random() * (max - min) + min;
}

function randInt(min, max) {
    return Math.floor(Math.random() * (max - min)) + min;
}