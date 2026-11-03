#include <unistd.h>
#include <string.h>
static void mark(const char *name) { write(2,name,strlen(name)); }
__attribute__((visibility("default"))) int am_active(void *self, unsigned int uid, const void *pkg) __asm__("_ZN7android15ActivityManager11isUidActiveEjRKNS_8String16E");
int am_active(void *self, unsigned int uid, const void *pkg) { mark("CAM_AM isUidActive\n"); return 1; }
#define AM_STATUS(name,sym) __attribute__((visibility("default"))) int name(void *self,...) __asm__(sym); int name(void *self,...) { mark("CAM_AM " #name "\n"); return -32; }
AM_STATUS(am_register,"_ZN7android15ActivityManager26registerUidObserverForUidsERKNS_2spINS_12IUidObserverEEEiiRKNS_8String16EPKimRNS1_INS_7IBinderEEE")
AM_STATUS(am_unreg,"_ZN7android15ActivityManager21unregisterUidObserverERKNS_2spINS_12IUidObserverEEE")
AM_STATUS(am_link,"_ZN7android15ActivityManager11linkToDeathERKNS_2spINS_7IBinder14DeathRecipientEEE")
AM_STATUS(am_unlink,"_ZN7android15ActivityManager13unlinkToDeathERKNS_2spINS_7IBinder14DeathRecipientEEE")
AM_STATUS(am_add,"_ZN7android15ActivityManager16addUidToObserverERKNS_2spINS_7IBinderEEERKNS_8String16Ei")
AM_STATUS(am_remove,"_ZN7android15ActivityManager21removeUidFromObserverERKNS_2spINS_7IBinderEEERKNS_8String16Ei")
