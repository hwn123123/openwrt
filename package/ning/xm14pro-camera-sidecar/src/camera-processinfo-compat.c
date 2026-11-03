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
    (void)binder;
    if (code != 1 && code != 2) return STATUS_UNKNOWN_TRANSACTION;
    fprintf(stderr, "processinfo transaction code=%u\n", (unsigned)code);
    if (out) {
        int32_t length = 0, pid = 0;
        if (AParcel_readInt32(in, &length) != STATUS_OK || length < 0 || length > 64) {
            fprintf(stderr, "processinfo invalid length=%d\n", length);
            return STATUS_BAD_VALUE;
        }
        for (int32_t i = 0; i < length; ++i) AParcel_readInt32(in, &pid);
        int32_t output1 = -1, output2 = -1;
        AParcel_readInt32(in, &output1);
        if (code == 2) AParcel_readInt32(in, &output2);
        fprintf(stderr, "processinfo pids=%d last_pid=%d output lengths=%d,%d\n",
                length, pid, output1, output2);
        AStatus *status = AStatus_newOk();
        binder_status_t result = AParcel_writeStatusHeader(out, status);
        AStatus_delete(status);
        if (result != STATUS_OK) return result;
        int32_t states[64], scores[64];
        for (int32_t i=0; i<length; ++i) { states[i] = 2; scores[i] = 0; }
        result = AParcel_writeInt32Array(out, states, length);
        if (result != STATUS_OK) return result;
        if (code == 2) {
            result = AParcel_writeInt32Array(out, scores, length);
            if (result != STATUS_OK) return result;
        }
        return AParcel_writeInt32(out, 0);
    }
    return STATUS_OK;
}
int main(void) {
    fprintf(stderr, "camera proxy main entered\n");
    AIBinder_Class *clazz = AIBinder_Class_define(
        "android.os.IProcessInfoService", create, destroy, transact);
    if (!clazz) { fprintf(stderr, "camera proxy class failed\n"); return 1; }
    fprintf(stderr, "camera proxy class ready\n");
    AIBinder *binder = AIBinder_new(clazz, NULL);
    if (!binder) { fprintf(stderr, "camera proxy binder failed\n"); return 2; }
    fprintf(stderr, "camera proxy binder ready\n");
    ABinderProcess_setThreadPoolMaxThreadCount(4);
    fprintf(stderr, "camera proxy thread count configured\n");
    binder_exception_t result = AServiceManager_addService(binder, "processinfo");
    fprintf(stderr, "camera proxy registration=%d\n", result);
    if (result != EX_NONE) return 3;
    ABinderProcess_startThreadPool();
    ABinderProcess_joinThreadPool();
    return 0;
}
