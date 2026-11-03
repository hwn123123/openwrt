#include <android/binder_ibinder.h>
#include <android/binder_parcel.h>
#include <android/binder_status.h>
#include <stdint.h>
#include <stdio.h>
extern binder_exception_t AServiceManager_addService(AIBinder *, const char *);
extern bool ABinderProcess_setThreadPoolMaxThreadCount(uint32_t);
extern void ABinderProcess_startThreadPool(void);
extern void ABinderProcess_joinThreadPool(void);

static void *create(void *args) { return args; }
static void destroy(void *data) { (void)data; }
static binder_status_t transact(AIBinder *binder, transaction_code_t code,
                                const AParcel *in, AParcel *out) {
    (void)binder; (void)in;
    if (code != 1 && code != 2 && code != 6) return STATUS_UNKNOWN_TRANSACTION;
    fprintf(stderr, "camera proxy transaction code=%u\n", (unsigned)code);
    if (out) {
        AStatus *status = AStatus_newOk();
        binder_status_t result = AParcel_writeStatusHeader(out, status);
        AStatus_delete(status);
        if (result != STATUS_OK) return result;
        return AParcel_writeInt32(out, 0);
    }
    return STATUS_OK;
}
int main(void) {
    fprintf(stderr, "camera proxy main entered\n");
    AIBinder_Class *clazz = AIBinder_Class_define(
        "android.hardware.ICameraServiceProxy", create, destroy, transact);
    if (!clazz) { fprintf(stderr, "camera proxy class failed\n"); return 1; }
    fprintf(stderr, "camera proxy class ready\n");
    AIBinder *binder = AIBinder_new(clazz, NULL);
    if (!binder) { fprintf(stderr, "camera proxy binder failed\n"); return 2; }
    fprintf(stderr, "camera proxy binder ready\n");
    ABinderProcess_setThreadPoolMaxThreadCount(4);
    fprintf(stderr, "camera proxy thread count configured\n");
    binder_exception_t result = AServiceManager_addService(binder, "media.camera.proxy");
    fprintf(stderr, "camera proxy registration=%d\n", result);
    if (result != EX_NONE) return 3;
    ABinderProcess_startThreadPool();
    ABinderProcess_joinThreadPool();
    return 0;
}
