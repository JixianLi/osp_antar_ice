#pragma once

#include <ospray/ospray_cpp.h>

#include "ospr/script.h"

namespace ospr {

// Builds the committed world named by spec.type. Throws std::runtime_error for
// an unknown type.
ospray::cpp::World build_world(const SceneSpec& spec);

} // namespace ospr
