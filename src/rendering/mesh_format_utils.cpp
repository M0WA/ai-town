#include "mesh_format_utils.h"
#include <IFileSystem.h>   // irr::io::IFileSystem -- full definition
#include <filesystem>

std::string resolveModelPath(irr::io::IFileSystem* fs,
                             const std::string& basePath,
                             const std::string& suffix) {
    std::string ply = basePath + suffix + ".ply";
    if (fs) {
        if (fs->existFile(ply.c_str())) return ply;
    } else {
        if (std::filesystem::exists(ply)) return ply;
    }
    return basePath + suffix + ".b3d";
}
