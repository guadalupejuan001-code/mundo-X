#pragma once

#include "mundo_x/infinite_bridge.h"
#include <memory>

namespace mundo_x {
class InfiniteSession;

// Create an independently owned C handle to the same C++ session. The caller
// releases this owner with mxi_destroy; other shared owners remain valid.
// Allocation and invalid arguments may throw. These functions are implemented
// once in the bridge library, which PC/JNI consumers link instead of copying.
void* share_infinite_session(std::shared_ptr<InfiniteSession> session);
std::shared_ptr<InfiniteSession> infinite_session_from_handle(void* handle);
}  // namespace mundo_x
