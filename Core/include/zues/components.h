#pragma once

// Umbrella for engine-defined component TYPES. Behavior (systems that
// operate on them) lives in the owning module — render systems in
// zues_renderer_*, physics systems in zues_physics_*, etc. Including this
// header gives access to the types from anywhere.

#include <zues/components/transform.h>
#include <zues/components/hierarchy.h>
#include <zues/components/name.h>
#include <zues/components/render.h>
#include <zues/components/physics.h>
#include <zues/components/audio.h>
