//
//  VoodooI2CNativeEngine.cpp
//  VoodooI2C
//
//  Created by Alexandre on 10/02/2018.
//  Copyright © 2018 Alexandre Daoud and Kishor Prins. All rights reserved.
//

#include "VoodooI2CNativeEngine.hpp"

#define super VoodooI2CMultitouchEngine
OSDefineMetaClassAndStructors(VoodooI2CNativeEngine, VoodooI2CMultitouchEngine);

MultitouchReturn VoodooI2CNativeEngine::handleInterruptReport(VoodooI2CMultitouchEvent event, AbsoluteTime timestamp) {
    if (!voodooInputInstance) {
        return MultitouchReturnContinue;
    }

    message.timestamp = timestamp;
    message.contact_count = event.contact_count;
    memset(message.transducers, 0, VOODOO_INPUT_MAX_TRANSDUCERS * sizeof(VoodooInputTransducer));
    
    VoodooI2CDigitiserTransducer* transducer = (VoodooI2CDigitiserTransducer*) event.transducers->getObject(0);

    if (!transducer)
        return MultitouchReturnBreak;
    
    if (transducer->type == kDigitiserTransducerStylus)
        stylus_check = 1;

    int valid_touch_count = 0;

    for (int i = 0; i < event.contact_count; i++) {
        VoodooI2CDigitiserTransducer* transducer = (VoodooI2CDigitiserTransducer*) event.transducers->getObject(i+stylus_check);
        VoodooInputTransducer* inputTransducer = &message.transducers[i];
        
        if (!transducer) {
            continue;
        }
        
        inputTransducer->fingerType = (MT2FingerType) (kMT2FingerTypeIndexFinger + (i % 4));
        inputTransducer->secondaryId = transducer->secondary_id;
        
        inputTransducer->type = (transducer->type == DigitiserTransducerType::kDigitiserTransducerFinger) ? VoodooInputTransducerType::FINGER : VoodooInputTransducerType::STYLUS;
        
        inputTransducer->isValid = transducer->is_valid;
        if (inputTransducer->isValid) {
            valid_touch_count++;
        }
        inputTransducer->isTransducerActive = transducer->tip_switch.value();
        inputTransducer->isPhysicalButtonDown = transducer->physical_button.value();
        
        inputTransducer->currentCoordinates.x = transducer->coordinates.x.value();
        inputTransducer->previousCoordinates.x = transducer->coordinates.x.last.value;
        
        inputTransducer->currentCoordinates.y = transducer->coordinates.y.value();
        inputTransducer->previousCoordinates.y = transducer->coordinates.y.last.value;
        inputTransducer->supportsPressure = false;
        inputTransducer->timestamp = timestamp;

        // The HID pressure can span the device's whole logical range (the Surface Laptop Studio touchpad reports
        // 0..2000), but the emulated Magic Trackpad 2 carries pressure and contact size as single bytes: a high
        // pressure is a force click and a large size is a palm, so raw values wrap into "palm pressing hard" and
        // clicks turn into right/double/force clicks. Scale to 0..255, keep normal touches under the force-click
        // range, and derive a small contact size from it (pads that report no pressure get 10, as the force-touch
        // emulation below already uses).
        UInt32 pmax = transducer->pressure_physical_max > 0 ? (UInt32) transducer->pressure_physical_max : 255;
        UInt32 praw = transducer->tip_pressure.value(), plast = transducer->tip_pressure.last.value;
        UInt32 pcur = praw > pmax ? 255 : praw * 255 / pmax;
        UInt32 pprev = plast > pmax ? 255 : plast * 255 / pmax;
        inputTransducer->currentCoordinates.width = 10 + pcur / 16;
        inputTransducer->previousCoordinates.width = 10 + pprev / 16;
        // Any non-zero pressure here is read by macOS as a (partial) force click ("3D touch" feel on every click), so a
        // plain touch reports 0; only the force-touch emulation below raises it, and only when the button is down.
        inputTransducer->currentCoordinates.pressure = 0;
        inputTransducer->previousCoordinates.pressure = 0;

        // Force Touch emulation
        // The button state is saved in the first transducer
        if (((VoodooI2CDigitiserTransducer*) event.transducers->getObject(0))->physical_button.value() && isForceClickEnabled()) {
            inputTransducer->supportsPressure = true;
            inputTransducer->isPhysicalButtonDown = 0x0;
            inputTransducer->currentCoordinates.pressure = 0xff;
            inputTransducer->currentCoordinates.width = 10;
        }
    }
    
    // set the thumb to improve 4F pinch and spread gesture and cross-screen dragging
    if (valid_touch_count >= 4 || transducer->physical_button.value()) {
        // simple thumb detection: to find the lowest finger touch in the vertical direction.
        UInt32 y_max = 0;
        int thumb_index = 0;
        for (int i = 0; i < event.contact_count; i++) {
            VoodooInputTransducer* inputTransducer = &message.transducers[i];
            if (inputTransducer->isValid && inputTransducer->currentCoordinates.y >= y_max) {
                y_max = inputTransducer->currentCoordinates.y;
                thumb_index = i;
            }
        }
        message.transducers[thumb_index].fingerType = kMT2FingerTypeThumb;
    }
        
    super::messageClient(kIOMessageVoodooInputMessage, voodooInputInstance, &message, sizeof(VoodooInputEvent));
    
    return MultitouchReturnBreak;
}

bool VoodooI2CNativeEngine::start(IOService* provider) {
    if (!super::start(provider))
        return false;

    parentProvider = OSDynamicCast(VoodooI2CMultitouchInterface, provider);
    
    if (!parentProvider) {
        return false;
    }

    voodooInputInstance = NULL;
    
    setProperty(VOODOO_INPUT_LOGICAL_MAX_X_KEY, parentProvider->logical_max_x, 32);
    setProperty(VOODOO_INPUT_LOGICAL_MAX_Y_KEY, parentProvider->logical_max_y, 32);
    
    setProperty(VOODOO_INPUT_PHYSICAL_MAX_X_KEY, parentProvider->physical_max_x, 32);
    setProperty(VOODOO_INPUT_PHYSICAL_MAX_Y_KEY, parentProvider->physical_max_y, 32);

    setProperty("VoodooInputSupported", kOSBooleanTrue);
    
    stylus_check = 0;

    return true;
}

bool VoodooI2CNativeEngine::isForceClickEnabled() {
    AbsoluteTime now_abs;
    UInt64 diff_ns;
    clock_get_uptime(&now_abs);
    absolutetime_to_nanoseconds(now_abs - lastForceClickPropertyUpdateTime, &diff_ns);

    if (diff_ns < 1e9) {
        lastForceClickPropertyUpdateTime = now_abs;
        return lastIsForceClickEnabled;
    }

    // Blocking reading here takes about 10 microseconds
    OSDictionary* dict = OSDynamicCast(OSDictionary, this->getProperty("MultitouchPreferences", gIOServicePlane, kIORegistryIterateRecursively));
    if (!dict) {
        return lastIsForceClickEnabled;
    }
    if (OSCollectionIterator* i = OSCollectionIterator::withCollection(dict)) {
        while (OSSymbol* key = OSDynamicCast(OSSymbol, i->getNextObject())) {
            // System -> Preferences -> Trackpad -> Force Click and haptic feedback
            // ForceSuppressed
            if (key->isEqualTo("ForceSuppressed")) {
                OSBoolean* value = OSDynamicCast(OSBoolean, dict->getObject(key));

                if (value != NULL) {
                    lastIsForceClickEnabled = !value->getValue();
                }
            }
        }

        i->release();
    }

    lastForceClickPropertyUpdateTime = now_abs;
    return lastIsForceClickEnabled;
}

void VoodooI2CNativeEngine::onPropertyChange() {
    if (voodooInputInstance) {
        super::messageClient(kIOMessageVoodooInputUpdatePropertiesNotification, voodooInputInstance);
    }
}

void VoodooI2CNativeEngine::stop(IOService* provider) {
    super::stop(provider);
}

bool VoodooI2CNativeEngine::handleOpen(IOService *forClient, IOOptionBits options, void *arg) {
    if (forClient && forClient->getProperty(VOODOO_INPUT_IDENTIFIER)) {
        voodooInputInstance = forClient;
        voodooInputInstance->retain();
        
        return true;
    }
    return false;
}

bool VoodooI2CNativeEngine::handleIsOpen(const IOService *forClient) const {
    return voodooInputInstance != NULL && forClient == voodooInputInstance;
}

void VoodooI2CNativeEngine::handleClose(IOService *forClient, IOOptionBits options) {
    if (voodooInputInstance && forClient == voodooInputInstance) {
        OSSafeReleaseNULL(voodooInputInstance);
    }
}

bool VoodooI2CNativeEngine::willTerminate(IOService *provider, IOOptionBits options) {
    return super::willTerminate(provider, options);
}
