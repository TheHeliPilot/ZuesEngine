// HostContext storage. Defined here so HostShared has a TU that owns the
// `g_host_ctx` symbol; both editor and runtime link the same instance.
//
// host_api.cpp keeps a separate `g_host` static that mirrors this pointer
// for hot-path access -- set_host_context updates both at once.

#include <zues/host/host_context.h>
#include <zues/host/host_api.h>

namespace Engine::host {

// Defined inside host_api.cpp to keep all the host_api globals in one TU.
// host_api.cpp's set_host_context populates both the public pointer and
// its own g_host/g_world mirrors.

}  // namespace Engine::host
