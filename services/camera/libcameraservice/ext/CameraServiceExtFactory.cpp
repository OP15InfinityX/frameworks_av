#include "CameraServiceExtFactory.h"
#include "CameraService.h"
#include <android-base/properties.h>
#include <dlfcn.h>
#include <log/log.h>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>

namespace android {

namespace {

struct TrackedCameraClient {
    wp<CameraService::BasicClient> client;
    std::string packageName;
};

std::mutex sTrackedClientMutex;
std::unordered_map<std::string, TrackedCameraClient> sTrackedClients;

std::string toUtf8(const String16& value) {
    return String8(value).c_str();
}

} // namespace

void* CameraServiceExtFactory::sFunctionTable = nullptr;
void* CameraServiceExtFactory::sExtObject = nullptr;
int (*CameraServiceExtFactory::sOnTransactFunc)(void*, uint32_t, const Parcel&, Parcel*, uint32_t) = nullptr;
void* CameraServiceExtFactory::sSetCameraServiceInstanceFunc = nullptr;
void* CameraServiceExtFactory::sBeforeConnectFunc = nullptr;
void* CameraServiceExtFactory::sAfterConnectFunc = nullptr;
void* CameraServiceExtFactory::sBeforeDisconnectFunc = nullptr;
void* CameraServiceExtFactory::sAfterDisconnectFunc = nullptr;

void CameraServiceExtFactory::ensureLoaded() {
    if (sFunctionTable != nullptr) return;

    const char* libPath = "system_ext/lib64/libcsextimpl.so";
    void* handle = dlopen(libPath, RTLD_NOW);
    if (handle == nullptr) {
        ALOGE("CameraServiceExtFactory: dlopen failed: %s", dlerror());
        return;
    }
    ALOGI("CameraServiceExtFactory: dlopen succeeded, handle=%p", handle);

    typedef void* (*GetFactoryFunc)();
    GetFactoryFunc getExtFactoryImpl = (GetFactoryFunc)dlsym(handle, "getExtFactoryImpl");
    if (getExtFactoryImpl == nullptr) {
        ALOGE("CameraServiceExtFactory: dlsym getExtFactoryImpl failed: %s", dlerror());
        dlclose(handle);
        return;
    }
    ALOGI("CameraServiceExtFactory: getExtFactoryImpl at %p", getExtFactoryImpl);

    // Triple indirection as determined from logs: getExtFactoryImpl returns ptr to ptr to ptr to function
    void* ptrToPtr = getExtFactoryImpl();
    if (ptrToPtr == nullptr) {
        ALOGE("CameraServiceExtFactory: getExtFactoryImpl returned null");
        dlclose(handle);
        return;
    }

    void* ptrToFunc = *(void**)ptrToPtr;
    if (ptrToFunc == nullptr) {
        ALOGE("CameraServiceExtFactory: first deref gave null");
        dlclose(handle);
        return;
    }

    void* actualFunc = *(void**)ptrToFunc;
    if (actualFunc == nullptr) {
        ALOGE("CameraServiceExtFactory: second deref gave null");
        dlclose(handle);
        return;
    }
    ALOGI("CameraServiceExtFactory: actual factory function at %p", actualFunc);

    sFunctionTable = operator new(8);
    *(void**)sFunctionTable = actualFunc;
    ALOGI("CameraServiceExtFactory: function table at %p", sFunctionTable);

    // Resolve onTransact (for direct call via vtable)
    sOnTransactFunc = (int (*)(void*, uint32_t, const Parcel&, Parcel*, uint32_t))
        dlsym(handle, "_ZN7android20CameraServiceExtImpl10onTransactEjRKNS_6ParcelEPS1_j");
    if (sOnTransactFunc == nullptr) {
        ALOGE("CameraServiceExtFactory: dlsym onTransact failed: %s", dlerror());
    } else {
        ALOGI("CameraServiceExtFactory: onTransact found at %p", sOnTransactFunc);
    }

    sSetCameraServiceInstanceFunc = dlsym(handle,
            "_ZN7android20CameraServiceExtImpl24setCameraServiceInstanceENS_2spINS_13CameraServiceEEE");
    sBeforeConnectFunc = dlsym(handle,
            "_ZN7android20CameraServiceExtImpl13beforeConnectERKNS_7String8ERKNS_8String16Eb");
    sAfterConnectFunc = dlsym(handle,
            "_ZN7android20CameraServiceExtImpl12afterConnectERKNS_7String8ERKNS_8String16EbNS_2spINS_13CameraService11BasicClientEEEPvmi");
    sBeforeDisconnectFunc = dlsym(handle,
            "_ZN7android20CameraServiceExtImpl16beforeDisconnectEbbRKNS_7String8E");
    sAfterDisconnectFunc = dlsym(handle,
            "_ZN7android20CameraServiceExtImpl15afterDisconnectENS_7String8ENS_8String16ENS_2spINS_13CameraServiceEEE");

    if (sSetCameraServiceInstanceFunc == nullptr || sBeforeConnectFunc == nullptr ||
            sAfterConnectFunc == nullptr || sBeforeDisconnectFunc == nullptr ||
            sAfterDisconnectFunc == nullptr) {
        ALOGE("CameraServiceExtFactory: one or more lifecycle hooks are unavailable: %s",
                dlerror());
    } else {
        ALOGI("CameraServiceExtFactory: camera lifecycle hooks loaded");
    }
}

void* CameraServiceExtFactory::getInstance() {
    ensureLoaded();
    return sFunctionTable;   // may be null
}

void* CameraServiceExtFactory::getExtObject() {
    ensureLoaded();
    if (sExtObject == nullptr) {
        if (sFunctionTable == nullptr) {
            ALOGE("CameraServiceExtFactory::getExtObject: extension not loaded");
            return nullptr;
        }
        void* actualFunc = *(void**)sFunctionTable;
        if (actualFunc == nullptr) return nullptr;
        typedef void* (*GetObjectFunc)();
        sExtObject = ((GetObjectFunc)actualFunc)();
        if (sExtObject == nullptr) {
            ALOGE("CameraServiceExtFactory: factory returned null");
            return nullptr;
        }
        ALOGI("CameraServiceExtFactory: real extension object at %p", sExtObject);
    }
    return sExtObject;
}

int CameraServiceExtFactory::onTransact(uint32_t code, const Parcel& data, Parcel* reply,
        uint32_t flags) {
    void* extObject = getExtObject();
    if (extObject == nullptr) return -1;

    if (sOnTransactFunc == nullptr) {
        ALOGE("CameraServiceExtFactory::onTransact: no function pointer");
        return -1;
    }
    return sOnTransactFunc(extObject, code, data, reply, flags);
}

void CameraServiceExtFactory::setCameraServiceInstance(const sp<CameraService>& service) {
    void* extObject = getExtObject();
    if (extObject == nullptr || sSetCameraServiceInstanceFunc == nullptr) return;
    using Func = void (*)(void*, sp<CameraService>);
    reinterpret_cast<Func>(sSetCameraServiceInstanceFunc)(extObject, service);
}

int CameraServiceExtFactory::beforeConnect(const String8& cameraId,
        const String16& packageName, bool shimUpdateOnly) {
    void* extObject = getExtObject();
    int status = 0;
    if (extObject != nullptr && sBeforeConnectFunc != nullptr) {
        using Func = int (*)(void*, const String8&, const String16&, bool);
        status = reinterpret_cast<Func>(sBeforeConnectFunc)(
                extObject, cameraId, packageName, shimUpdateOnly);
    }

    if (status != 0 || shimUpdateOnly) return status;

    const std::string newPackage = toUtf8(packageName);
    const std::string systemCameraPackage = android::base::GetProperty(
            "ro.oplus.system.camera.name", "com.oplus.camera");
    if (newPackage != systemCameraPackage) return status;

    sp<CameraService::BasicClient> clientToDisconnect;
    std::string oldPackage;
    {
        std::lock_guard<std::mutex> lock(sTrackedClientMutex);
        const auto it = sTrackedClients.find(cameraId.c_str());
        if (it != sTrackedClients.end() && it->second.packageName != newPackage) {
            clientToDisconnect = it->second.client.promote();
            oldPackage = it->second.packageName;
        }
    }

    if (clientToDisconnect != nullptr) {
        ALOGI("CameraServiceExtFactory: evicting camera %s client %s for system camera %s",
                cameraId.c_str(), oldPackage.c_str(), newPackage.c_str());
        // block() is the server-initiated close path: it makes the caller valid,
        // notifies the old app, and does not return until its HAL session is closed.
        clientToDisconnect->block();
    }

    return status;
}

void CameraServiceExtFactory::afterConnect(const String8& cameraId,
        const String16& packageName, bool shimUpdateOnly, void* client, void* cameraDevice,
        size_t vendorTagId, int apiLevel) {
    void* extObject = getExtObject();
    if (extObject == nullptr || sAfterConnectFunc == nullptr || client == nullptr) return;

    using Func = bool (*)(void*, const String8&, const String16&, bool,
            sp<CameraService::BasicClient>, void*, size_t, int);
    sp<CameraService::BasicClient> clientSp = sp<CameraService::BasicClient>::fromExisting(
            static_cast<CameraService::BasicClient*>(client));
    (void)reinterpret_cast<Func>(sAfterConnectFunc)(extObject, cameraId, packageName,
            shimUpdateOnly, clientSp, cameraDevice, vendorTagId, apiLevel);

    if (!shimUpdateOnly) {
        std::lock_guard<std::mutex> lock(sTrackedClientMutex);
        sTrackedClients[cameraId.c_str()] = {clientSp, toUtf8(packageName)};
    }
}

bool CameraServiceExtFactory::beforeDisconnect(bool invalidCaller, bool skipDeviceDisconnect,
        const String8& cameraId) {
    void* extObject = getExtObject();
    if (extObject == nullptr || sBeforeDisconnectFunc == nullptr) return true;
    using Func = bool (*)(void*, bool, bool, const String8&);
    return reinterpret_cast<Func>(sBeforeDisconnectFunc)(
            extObject, invalidCaller, skipDeviceDisconnect, cameraId);
}

void CameraServiceExtFactory::afterDisconnect(String8 cameraId, String16 packageName,
        const sp<CameraService>& service) {
    void* extObject = getExtObject();
    if (extObject != nullptr && sAfterDisconnectFunc != nullptr) {
        using Func = void (*)(void*, String8, String16, sp<CameraService>);
        reinterpret_cast<Func>(sAfterDisconnectFunc)(
                extObject, cameraId, packageName, service);
    }

    std::lock_guard<std::mutex> lock(sTrackedClientMutex);
    const auto it = sTrackedClients.find(cameraId.c_str());
    if (it != sTrackedClients.end() && it->second.packageName == toUtf8(packageName)) {
        sTrackedClients.erase(it);
    }
}

CameraServiceExtFactory::~CameraServiceExtFactory() {
    // No cleanup needed – the extension library manages its own singleton.
    ALOGV("CameraServiceExtFactory destructor (stub)");
}

} // namespace android
