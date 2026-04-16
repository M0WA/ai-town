#pragma once
#include <string>

namespace irr { namespace io { class IFileSystem; } }

/// Try PLY first, fall back to B3D.
/// fs:       Irrlicht VFS handle (may be nullptr for offline tools / unit tests).
/// basePath: e.g. "assets/3d/vehicles/car_sedan"
/// suffix:   e.g. "_lod0"
/// Returns:  basePath + suffix + ".ply"  if that file exists,
///           basePath + suffix + ".b3d"  otherwise.
std::string resolveModelPath(irr::io::IFileSystem* fs,
                             const std::string& basePath,
                             const std::string& suffix);
