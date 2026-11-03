#include <unistd.h>
#include <string.h>
static void mark(const char *s){write(2,s,strlen(s));}
#define ALLOW(name,sym) __attribute__((visibility("default"))) int name(void *self,...) __asm__(sym); int name(void *self,...) {mark("CAM_PERMISSION " #name " granted\n");return 0;}
ALLOW(preflight,"_ZN7android10permission17PermissionChecker27checkPermissionForPreflightERKNS_8String16ERKNS_7content22AttributionSourceStateES4_i")
ALLOW(delivery,"_ZN7android10permission17PermissionChecker44checkPermissionForDataDeliveryFromDatasourceERKNS_8String16ERKNS_7content22AttributionSourceStateES4_i")
ALLOW(startdelivery,"_ZN7android10permission17PermissionChecker49checkPermissionForStartDataDeliveryFromDatasourceERKNS_8String16ERKNS_7content22AttributionSourceStateES4_i")
