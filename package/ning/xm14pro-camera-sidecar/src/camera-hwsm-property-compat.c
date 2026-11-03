#include <dlfcn.h>
#include <string.h>

typedef int (*property_set_fn)(const char *, const char *);

int __system_property_set(const char *name, const char *value)
{
    static property_set_fn next;
    if (name && value && strcmp(name, "hwservicemanager.disabled") == 0 &&
        strcmp(value, "false") == 0)
        return 0;
    if (!next)
        next = (property_set_fn)dlsym(RTLD_NEXT, "__system_property_set");
    return next ? next(name, value) : -1;
}
