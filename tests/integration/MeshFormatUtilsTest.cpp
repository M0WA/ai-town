// tests/integration/MeshFormatUtilsTest.cpp
// Phase 11q12: Integration tests for resolveModelPath() — PLY-first mesh loading
// with B3D fallback.
//
// Six tests in two suites:
//   MeshFormatUtilsTest    (Tests 1–3): nullptr IFileSystem* — exercises std::filesystem::exists() path
//   MeshFormatUtilsIFSTest (Tests 4–6): EDT_NULL IFileSystem* — exercises IFileSystem::existFile() path
//
// Registered via target_sources(integration_tests ...) in CMakeLists.txt.
// CTest label: "integration" (no display required; no OpenGL context).
//
// Mock policy: no mocks — resolveModelPath() is a pure free function.
// TearDown: device_->drop() in IFS fixture per destructor-path contract.

#include "mesh_format_utils.h"

#include <irrlicht.h>

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include <filesystem>
#include <fstream>
#include <string>

using ::testing::EndsWith;

// ---------------------------------------------------------------------------
// Helper: create a unique temp directory for each test.
// ---------------------------------------------------------------------------
static std::string createUniqueTmpDir(const std::string& testTag) {
    auto base = std::filesystem::temp_directory_path() / ("mfu_test_" + testTag);
    // Remove if leftover from a previous run.
    std::filesystem::remove_all(base);
    std::filesystem::create_directories(base);
    return base.string();
}

static void removeTmpDir(const std::string& dir) {
    std::filesystem::remove_all(dir);
}

// Touch an empty file at the given path.
static void touchFile(const std::string& path) {
    std::ofstream ofs(path);
    ofs.close();
}

// ===========================================================================
// Suite 1: MeshFormatUtilsTest — nullptr IFileSystem* (std::filesystem fallback)
// ===========================================================================

TEST(MeshFormatUtilsTest, NullFS_PLYPresent_ReturnsPLYPath) {
    std::string tmpDir = createUniqueTmpDir("nullfs_ply");
    touchFile(tmpDir + "/foo_lod0.ply");

    std::string result = resolveModelPath(nullptr, tmpDir + "/foo", "_lod0");
    EXPECT_THAT(result, EndsWith(".ply"));

    removeTmpDir(tmpDir);
}

TEST(MeshFormatUtilsTest, NullFS_B3DOnly_ReturnsB3DPath) {
    std::string tmpDir = createUniqueTmpDir("nullfs_b3d");
    touchFile(tmpDir + "/foo_lod0.b3d");

    std::string result = resolveModelPath(nullptr, tmpDir + "/foo", "_lod0");
    EXPECT_THAT(result, EndsWith(".b3d"));

    removeTmpDir(tmpDir);
}

TEST(MeshFormatUtilsTest, NullFS_NeitherPresent_ReturnsB3DPath) {
    std::string tmpDir = createUniqueTmpDir("nullfs_neither");
    // No files created — empty directory.

    std::string result = resolveModelPath(nullptr, tmpDir + "/foo", "_lod0");
    EXPECT_THAT(result, EndsWith(".b3d"));

    removeTmpDir(tmpDir);
}

// ===========================================================================
// Suite 2: MeshFormatUtilsIFSTest — EDT_NULL IFileSystem* path
// ===========================================================================

class MeshFormatUtilsIFSTest : public ::testing::Test {
protected:
    irr::IrrlichtDevice* device_{nullptr};
    std::string tmpDir_;

    void SetUp() override {
        device_ = irr::createDevice(
            irr::video::EDT_NULL,
            irr::core::dimension2d<irr::u32>(640, 480));
        ASSERT_NE(device_, nullptr) << "EDT_NULL device creation failed";

        tmpDir_ = createUniqueTmpDir("ifs_" + std::to_string(
            std::hash<const void*>{}(this)));
    }

    void TearDown() override {
        removeTmpDir(tmpDir_);
        if (device_) {
            device_->drop();
            device_ = nullptr;
        }
    }
};

TEST_F(MeshFormatUtilsIFSTest, IFileSystem_PLYPresent_ReturnsPLYPath) {
    touchFile(tmpDir_ + "/foo_lod0.ply");

    irr::io::IFileSystem* fs = device_->getFileSystem();
    ASSERT_NE(fs, nullptr);
    fs->addFolderFileArchive(tmpDir_.c_str());

    std::string result = resolveModelPath(fs, tmpDir_ + "/foo", "_lod0");
    EXPECT_THAT(result, EndsWith(".ply"));
}

TEST_F(MeshFormatUtilsIFSTest, IFileSystem_B3DOnly_ReturnsB3DPath) {
    touchFile(tmpDir_ + "/foo_lod0.b3d");

    irr::io::IFileSystem* fs = device_->getFileSystem();
    ASSERT_NE(fs, nullptr);
    fs->addFolderFileArchive(tmpDir_.c_str());

    std::string result = resolveModelPath(fs, tmpDir_ + "/foo", "_lod0");
    EXPECT_THAT(result, EndsWith(".b3d"));
}

TEST_F(MeshFormatUtilsIFSTest, IFileSystem_NeitherPresent_ReturnsB3DPath) {
    // Empty temp dir — no files created.
    irr::io::IFileSystem* fs = device_->getFileSystem();
    ASSERT_NE(fs, nullptr);
    fs->addFolderFileArchive(tmpDir_.c_str());

    std::string result = resolveModelPath(fs, tmpDir_ + "/foo", "_lod0");
    EXPECT_THAT(result, EndsWith(".b3d"));
}
