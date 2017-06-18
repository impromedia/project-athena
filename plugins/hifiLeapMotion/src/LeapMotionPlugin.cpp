//
//  LeapMotionPlugin.cpp
//
//  Created by David Rowe on 15 Jun 2017.
//  Copyright 2017 High Fidelity, Inc.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#include "LeapMotionPlugin.h"

#include <QLoggingCategory>

#include <controllers/UserInputMapper.h>
#include <NumericalConstants.h>
#include <PathUtils.h>
#include <Preferences.h>
#include <SettingHandle.h>

Q_DECLARE_LOGGING_CATEGORY(inputplugins)
Q_LOGGING_CATEGORY(inputplugins, "hifi.inputplugins")

const char* LeapMotionPlugin::NAME = "Leap Motion";
const char* LeapMotionPlugin::LEAPMOTION_ID_STRING = "Leap Motion";

const bool DEFAULT_ENABLED = false;
const char* SENSOR_ON_DESKTOP = "Desktop";
const char* SENSOR_ON_HMD = "HMD";
const char* DEFAULT_SENSOR_LOCATION = SENSOR_ON_DESKTOP;

enum LeapMotionJointIndex {
    LeftHand = 0,
    RightHand,
    Size
};

static controller::StandardPoseChannel LeapMotionJointIndexToPoseIndexMap[LeapMotionJointIndex::Size] = {
    controller::LEFT_HAND,
    controller::RIGHT_HAND
};

#define UNKNOWN_JOINT (controller::StandardPoseChannel)0 

static controller::StandardPoseChannel LeapMotionJointIndexToPoseIndex(LeapMotionJointIndex i) {
    assert(i >= 0 && i < LeapMotionJointIndex::Size);
    if (i >= 0 && i < LeapMotionJointIndex::Size) {
        return LeapMotionJointIndexToPoseIndexMap[i];
    } else {
        return UNKNOWN_JOINT;
    }
}

QStringList controllerJointNames = {
    "Hips",
    "RightUpLeg",
    "RightLeg",
    "RightFoot",
    "LeftUpLeg",
    "LeftLeg",
    "LeftFoot",
    "Spine",
    "Spine1",
    "Spine2",
    "Spine3",
    "Neck",
    "Head",
    "RightShoulder",
    "RightArm",
    "RightForeArm",
    "RightHand",
    "RightHandThumb1",
    "RightHandThumb2",
    "RightHandThumb3",
    "RightHandThumb4",
    "RightHandIndex1",
    "RightHandIndex2",
    "RightHandIndex3",
    "RightHandIndex4",
    "RightHandMiddle1",
    "RightHandMiddle2",
    "RightHandMiddle3",
    "RightHandMiddle4",
    "RightHandRing1",
    "RightHandRing2",
    "RightHandRing3",
    "RightHandRing4",
    "RightHandPinky1",
    "RightHandPinky2",
    "RightHandPinky3",
    "RightHandPinky4",
    "LeftShoulder",
    "LeftArm",
    "LeftForeArm",
    "LeftHand",
    "LeftHandThumb1",
    "LeftHandThumb2",
    "LeftHandThumb3",
    "LeftHandThumb4",
    "LeftHandIndex1",
    "LeftHandIndex2",
    "LeftHandIndex3",
    "LeftHandIndex4",
    "LeftHandMiddle1",
    "LeftHandMiddle2",
    "LeftHandMiddle3",
    "LeftHandMiddle4",
    "LeftHandRing1",
    "LeftHandRing2",
    "LeftHandRing3",
    "LeftHandRing4",
    "LeftHandPinky1",
    "LeftHandPinky2",
    "LeftHandPinky3",
    "LeftHandPinky4"
};

static const char* getControllerJointName(controller::StandardPoseChannel i) {
    if (i >= 0 && i < controller::NUM_STANDARD_POSES) {
        return qPrintable(controllerJointNames[i]);
    }
    return "unknown";
}


void LeapMotionPlugin::pluginUpdate(float deltaTime, const controller::InputCalibrationData& inputCalibrationData) {
    if (!_enabled) {
        return;
    }

    const auto frame = _controller.frame();
    const auto frameID = frame.id();
    if (_lastFrameID >= frameID) {
        // Leap Motion not connected or duplicate frame.
        return;
    }

    if (!_hasLeapMotionBeenConnected) {
        emit deviceConnected(getName());
        _hasLeapMotionBeenConnected = true;
    }

    processFrame(frame);  // Updates _joints.

    auto joints = _joints;

    auto userInputMapper = DependencyManager::get<controller::UserInputMapper>();
    userInputMapper->withLock([&, this]() {
        _inputDevice->update(deltaTime, inputCalibrationData, joints, _prevJoints);
    });

    _prevJoints = joints;

    _lastFrameID = frameID;
}

controller::Input::NamedVector LeapMotionPlugin::InputDevice::getAvailableInputs() const {
    static controller::Input::NamedVector availableInputs;
    if (availableInputs.size() == 0) {
        for (int i = 0; i < LeapMotionJointIndex::Size; i++) {
            auto channel = LeapMotionJointIndexToPoseIndex(static_cast<LeapMotionJointIndex>(i));
            availableInputs.push_back(makePair(channel, getControllerJointName(channel)));
        }
    };
    return availableInputs;
}

QString LeapMotionPlugin::InputDevice::getDefaultMappingConfig() const {
    static const QString MAPPING_JSON = PathUtils::resourcesPath() + "/controllers/leapmotion.json";
    return MAPPING_JSON;
}

void LeapMotionPlugin::InputDevice::update(float deltaTime, const controller::InputCalibrationData& inputCalibrationData,
        const std::vector<LeapMotionPlugin::LeapMotionJoint>& joints,
        const std::vector<LeapMotionPlugin::LeapMotionJoint>& prevJoints) {

    glm::mat4 controllerToAvatar = glm::inverse(inputCalibrationData.avatarMat) * inputCalibrationData.sensorToWorldMat;
    glm::quat controllerToAvatarRotation = glmExtractRotation(controllerToAvatar);

    // Desktop "zero" position is some distance above the Leap Motion sensor and half the avatar's shoulder-to-hand length in 
    // front of avatar.
    float halfShouldToHandLength = fabsf(extractTranslation(inputCalibrationData.defaultLeftHand).x 
        - extractTranslation(inputCalibrationData.defaultLeftArm).x) / 2.0f;
    const float ZERO_HEIGHT_OFFSET = 0.2f;
    glm::vec3 leapMotionOffset = glm::vec3(0.0f, ZERO_HEIGHT_OFFSET, halfShouldToHandLength);

    for (size_t i = 0; i < joints.size(); i++) {
        int poseIndex = LeapMotionJointIndexToPoseIndex((LeapMotionJointIndex)i);

        if (joints[i].position == Vectors::ZERO) {
            _poseStateMap[poseIndex] = controller::Pose();
            continue;
        }

        const glm::vec3& pos = controllerToAvatarRotation * (joints[i].position - leapMotionOffset);
        glm::quat rot = controllerToAvatarRotation * joints[i].orientation;

        glm::vec3 linearVelocity, angularVelocity;
        if (i < prevJoints.size()) {
            linearVelocity = (pos - (prevJoints[i].position * METERS_PER_CENTIMETER)) / deltaTime;  // m/s
            // quat log imaginary part points along the axis of rotation, with length of one half the angle of rotation.
            glm::quat d = glm::log(rot * glm::inverse(prevJoints[i].orientation));
            angularVelocity = glm::vec3(d.x, d.y, d.z) / (0.5f * deltaTime); // radians/s
        }

        _poseStateMap[poseIndex] = controller::Pose(pos, rot, linearVelocity, angularVelocity);
    }
}

void LeapMotionPlugin::init() {
    loadSettings();

    auto preferences = DependencyManager::get<Preferences>();
    static const QString LEAPMOTION_PLUGIN { "Leap Motion" };
    {
        auto getter = [this]()->bool { return _enabled; };
        auto setter = [this](bool value) {
            _enabled = value;
            saveSettings();
            if (!_enabled) {
                auto userInputMapper = DependencyManager::get<controller::UserInputMapper>();
                userInputMapper->withLock([&, this]() {
                    _inputDevice->clearState();
                });
            }
        };
        auto preference = new CheckPreference(LEAPMOTION_PLUGIN, "Enabled", getter, setter);
        preferences->addPreference(preference);
    }
    {
        auto getter = [this]()->QString { return _sensorLocation; };
        auto setter = [this](QString value) {
            _sensorLocation = value;
            saveSettings();
            applySensorLocation();
        };
        auto preference = new ComboBoxPreference(LEAPMOTION_PLUGIN, "Sensor location", getter, setter);
        QStringList list = { SENSOR_ON_DESKTOP, SENSOR_ON_HMD };
        preference->setItems(list);
        preferences->addPreference(preference);
    }
}

bool LeapMotionPlugin::activate() {
    InputPlugin::activate();

    if (_enabled) {
        // Nothing required to be done to start up Leap Motion.

        auto userInputMapper = DependencyManager::get<controller::UserInputMapper>();
        userInputMapper->registerDevice(_inputDevice);

        if (_joints.size() != LeapMotionJointIndex::Size) {
            _joints.resize(LeapMotionJointIndex::Size, { glm::vec3(), glm::quat() });
        }

        return true;
    }

    return false;
}

void LeapMotionPlugin::deactivate() {
    if (_inputDevice->_deviceID != controller::Input::INVALID_DEVICE) {
        auto userInputMapper = DependencyManager::get<controller::UserInputMapper>();
        userInputMapper->removeDevice(_inputDevice->_deviceID);
    }

    InputPlugin::deactivate();
}

const char* SETTINGS_ENABLED_KEY = "enabled";
const char* SETTINGS_SENSOR_LOCATION_KEY = "sensorLocation";

void LeapMotionPlugin::saveSettings() const {
    Settings settings;
    QString idString = getID();
    settings.beginGroup(idString);
    {
        settings.setValue(QString(SETTINGS_ENABLED_KEY), _enabled);
        settings.setValue(QString(SETTINGS_SENSOR_LOCATION_KEY), _sensorLocation);
    }
    settings.endGroup();
}

void LeapMotionPlugin::loadSettings() {
    Settings settings;
    QString idString = getID();
    settings.beginGroup(idString);
    {
        _enabled = settings.value(SETTINGS_ENABLED_KEY, QVariant(DEFAULT_ENABLED)).toBool();
        _sensorLocation = settings.value(SETTINGS_SENSOR_LOCATION_KEY, QVariant(DEFAULT_SENSOR_LOCATION)).toString();
        applySensorLocation();
    }
    settings.endGroup();
}

void LeapMotionPlugin::InputDevice::clearState() {
    for (size_t i = 0; i < LeapMotionJointIndex::Size; i++) {
        int poseIndex = LeapMotionJointIndexToPoseIndex((LeapMotionJointIndex)i);
        _poseStateMap[poseIndex] = controller::Pose();
    }
}

void LeapMotionPlugin::applySensorLocation() {
    if (_sensorLocation == SENSOR_ON_HMD) {
        _controller.setPolicyFlags(Leap::Controller::POLICY_OPTIMIZE_HMD);
    } else {
        _controller.setPolicyFlags(Leap::Controller::POLICY_DEFAULT);
    }
}

const float LEFT_SIDE_SIGN = -1.0f;
const float RIGHT_SIDE_SIGN = 1.0f;

glm::quat LeapBasisToQuat(float sideSign, const Leap::Matrix& basis) {
    glm::vec3 xAxis = sideSign * glm::vec3(basis.xBasis.x, basis.xBasis.y, basis.xBasis.z);
    glm::vec3 yAxis = glm::vec3(basis.yBasis.x, basis.yBasis.y, basis.yBasis.z);
    glm::vec3 zAxis = glm::vec3(basis.zBasis.x, basis.zBasis.y, basis.zBasis.z);
    glm::quat orientation = (glm::quat_cast(glm::mat3(xAxis, yAxis, zAxis)));
    const glm::quat ZERO_HAND_ORIENTATION = glm::quat(glm::vec3(PI_OVER_TWO, PI, 0.0f));
    return orientation * ZERO_HAND_ORIENTATION;
}

glm::vec3 LeapVectorToVec3(const Leap::Vector& vec) {
    return glm::vec3(vec.x * METERS_PER_MILLIMETER, vec.y * METERS_PER_MILLIMETER, vec.z * METERS_PER_MILLIMETER);
}

void LeapMotionPlugin::processFrame(const Leap::Frame& frame) {
    // Default to uncontrolled.
    _joints[LeapMotionJointIndex::LeftHand].position = glm::vec3();
    _joints[LeapMotionJointIndex::RightHand].position = glm::vec3();

    auto hands = frame.hands();

    for (int i = 0; i < hands.count() && i < 2; i++) {
        auto hand = hands[i];
        auto arm = hand.arm();

        if (hands[i].isLeft()) {
            _joints[LeapMotionJointIndex::LeftHand].position = LeapVectorToVec3(hand.palmPosition());
            _joints[LeapMotionJointIndex::LeftHand].orientation = LeapBasisToQuat(LEFT_SIDE_SIGN, hand.basis());
        } else {
            _joints[LeapMotionJointIndex::RightHand].position = LeapVectorToVec3(hand.palmPosition());
            _joints[LeapMotionJointIndex::RightHand].orientation = LeapBasisToQuat(RIGHT_SIDE_SIGN, hand.basis());
        }
    }
}

