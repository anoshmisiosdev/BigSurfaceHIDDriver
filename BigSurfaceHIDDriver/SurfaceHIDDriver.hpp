//
//  SurfaceHIDDriver.hpp
//  SurfaceHID
//
//  Created by Xavier on 2022/5/16.
//  Copyright © 2022 Xia Shangning. All rights reserved.
//

#ifndef SurfaceHIDDriver_hpp
#define SurfaceHIDDriver_hpp

#include "../../../BigSurface/BigSurface/SurfaceSerialHubDevices/SurfaceHIDNub.hpp"

class SurfaceHIDDevice;

// Largest HID input report we accept from the SAM. The Surface Laptop Studio touchpad's precision report (ID 4) is
// 44 bytes and its vendor reports reach 64; the old 32-byte buffers truncated them and the HID parser dropped every one.
// The SSH message cache is 256 bytes, so nothing larger can arrive.
#define SURFACE_HID_REPORT_BUFFER 256

class EXPORT SurfaceHIDDriver : public IOService {
	OSDeclareDefaultStructors(SurfaceHIDDriver)

public:
	IOService *probe(IOService *provider, SInt32 *score) override;
	
	bool start(IOService *provider) override;
	
	void stop(IOService *provider) override;
    
    IOReturn getHIDDescriptor(SurfaceHIDDeviceType device, SurfaceHIDDescriptor *desc);
    
    IOReturn getHIDAttributes(SurfaceHIDDeviceType device, SurfaceHIDAttributes *attr);
    
    IOReturn getReportDescriptor(SurfaceHIDDeviceType device, UInt8 *buffer, UInt16 len);
    
    IOReturn getRawReport(SurfaceHIDDeviceType device, UInt8 report_id, UInt8 *buffer, UInt16 len);
    
    void setRawReport(SurfaceHIDDeviceType device, UInt8 report_id, bool feature, UInt8 *buffer, UInt16 len);
    
private:
    SurfaceHIDNub*          nub {nullptr};
    IOWorkLoop*             work_loop {nullptr};
    IOInterruptEventSource* kbd_interrupt {nullptr};
    IOInterruptEventSource* tpd_interrupt {nullptr};
    SurfaceHIDDevice*       keyboard {nullptr};
    SurfaceHIDDevice*       touchpad {nullptr};
    IOInterruptEventSource* aux_interrupt {nullptr};
    SurfaceHIDDevice*       aux_keyboard {nullptr};    // Surface Laptop Studio: HID instance 2
    
    bool    legacy {false};
    bool    has_aux {false};
    bool    ready {false};
    
    IOBufferMemoryDescriptor* kbd_report {nullptr};
    IOBufferMemoryDescriptor* tpd_report {nullptr};
    IOBufferMemoryDescriptor* aux_report {nullptr};
    
    void eventReceived(SurfaceHIDNub *sender, SurfaceHIDDeviceType device, UInt8 *buffer, UInt16 len);
    
    void keyboardInputReceived(IOInterruptEventSource *sender, int count);
    
    void auxKeyboardInputReceived(IOInterruptEventSource *sender, int count);
    
    void touchpadInputReceived(IOInterruptEventSource *sender, int count);
    
    void releaseResources();
};

#endif /* SurfaceHIDDriver_hpp */
