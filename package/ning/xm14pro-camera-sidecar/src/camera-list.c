#include <camera/NdkCameraManager.h>
#include <stdio.h>
int main(void) {
    fprintf(stderr, "camera client begin\n");
    ACameraManager *mgr = ACameraManager_create();
    if (!mgr) { fprintf(stderr, "camera manager null\n"); return 2; }
    ACameraIdList *ids = 0;
    camera_status_t rc = ACameraManager_getCameraIdList(mgr, &ids);
    fprintf(stderr, "camera list result=%d count=%d\n", rc, ids ? ids->numCameras : -1);
    if (ids) {
        for (int i=0; i<ids->numCameras; i++) fprintf(stderr, "camera %s\n", ids->cameraIds[i]);
        ACameraManager_deleteCameraIdList(ids);
    }
    ACameraManager_delete(mgr);
    return rc ? 1 : 0;
}
