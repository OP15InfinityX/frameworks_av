#pragma once

#include <binder/Parcel.h>
#include <utils/String16.h>
#include <utils/String8.h>
#include <utils/StrongPointer.h>

namespace android {

// Forward declaration – we will not define this class
class CameraService;
class ICameraServiceExt;

class CameraServiceExtFactory {
public:
    static void* getInstance();
    static int onTransact(uint32_t code, const Parcel& data, Parcel* reply, uint32_t flags);
    static void setCameraServiceInstance(const sp<CameraService>& service);
    static int beforeConnect(const String8& cameraId, const String16& packageName,
            bool shimUpdateOnly);
    static void afterConnect(const String8& cameraId, const String16& packageName,
            bool shimUpdateOnly, void* client, void* cameraDevice, size_t vendorTagId,
            int apiLevel);
    static bool beforeDisconnect(bool invalidCaller, bool skipDeviceDisconnect,
            const String8& cameraId);
    static void afterDisconnect(String8 cameraId, String16 packageName,
            const sp<CameraService>& service);
    virtual ~CameraServiceExtFactory();

private:
    static void ensureLoaded();
    static void* getExtObject();
    static void* sFunctionTable;
    static void* sExtObject;
    static int (*sOnTransactFunc)(void*, uint32_t, const Parcel&, Parcel*, uint32_t);
    static void* sSetCameraServiceInstanceFunc;
    static void* sBeforeConnectFunc;
    static void* sAfterConnectFunc;
    static void* sBeforeDisconnectFunc;
    static void* sAfterDisconnectFunc;
};

} // namespace android
