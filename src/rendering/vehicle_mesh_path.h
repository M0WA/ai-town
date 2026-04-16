#pragma once
#include "src/interfaces/simulation_types.h"  // ZoneType
#include "src/platform/PlatformUtils.h"        // getAssetsDir()
#include <array>
#include <string>

/// Returns the LOD0 mesh base path (extensionless — no .ply or .b3d suffix)
/// for the given zone type.
/// Commercial -> bus_standard_lod0 (extensionless), Industrial -> truck_cargo_lod0 (extensionless).
/// Residential: round-robins among car_sedan, car_hatchback, car_suv via
/// variantIdx % 3 (pass static_cast<int>(handle) % 3 for deterministic per-vehicle
/// assignment; pass 0 for the car_sedan default / fallback for unknown zones).
///
/// Callers in IrrlichtRenderer must wrap the returned path through
/// resolveModelPath() to append the correct .ply or .b3d extension.
///
/// IMPORTANT: The five vehicle_id strings below (car_sedan, car_hatchback, car_suv,
/// bus_standard, truck_cargo) MUST exactly match the "vehicle_id" entries in
/// tools/vehicle_atlas_registry.json. If a vehicle_id is renamed or added in the
/// registry, this function must be updated in lockstep to prevent silent drift.
inline std::string vehicleMeshPath(ZoneType zone, int variantIdx = 0) {
    const std::string kVehicleDir = getAssetsDir() + "/3d/vehicles/";
    if (zone == ZoneType::Commercial) return kVehicleDir + "bus_standard_lod0";
    if (zone == ZoneType::Industrial) return kVehicleDir + "truck_cargo_lod0";
    // Residential — round-robin across three car variants.
    static const std::array<const char*, 3> kResidential = {{
        "car_sedan_lod0",
        "car_hatchback_lod0",
        "car_suv_lod0",
    }};
    return kVehicleDir + kResidential[static_cast<unsigned>(variantIdx) % 3];
}
