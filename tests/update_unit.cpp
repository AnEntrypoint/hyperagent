#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>

#include "core/update.hpp"

namespace {

namespace fs = std::filesystem;
namespace update = hyperagent::core::update;

void require(bool condition, const std::string& reason)
{
    if (!condition) {
        std::cerr << "UPDATE_UNIT_FAIL " << reason << "\n";
        std::exit(1);
    }
}

bool throws(const std::function<void()>& action)
{
    try {
        action();
    } catch (const std::exception&) {
        return true;
    }
    return false;
}

void write_text(const fs::path& path, const std::string& bytes)
{
    fs::create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary) << bytes;
}

fs::path staged_tree(const fs::path& scratch, const std::string& version)
{
    const fs::path tree = scratch / "unpacked" / version / "hyperagent-x64-linux";
    write_text(tree / "bin" / "hyperagent", version);
    return tree;
}

}

int main()
{
    const fs::path scratch = fs::temp_directory_path() / "hyperagent-update-unit";
    fs::remove_all(scratch);
    fs::create_directories(scratch);

    const auto manifest = update::parse_manifest(R"({"version":"1.2.3","commit":"abc123","built":"2026-11-26T00:00:00Z",)"
        R"("repo":"AnEntrypoint/hyperagent","assets":{"x64-linux":{"file":"hyperagent-x64-linux.tar.gz",)"
        R"("sha256":"2cf24dba5fb0a30e26e83b2ac5b9e29e1b161e5c1fa7425e73043362938b9824","size":5,"version":"1.2.3"}}})");
    require(manifest.version == "1.2.3", "manifest version");
    require(manifest.commit == "abc123", "manifest commit");
    require(manifest.assets.count("x64-linux") == 1, "manifest asset keyed by target");
    const update::Asset& asset = manifest.assets.at("x64-linux");
    require(asset.file == "hyperagent-x64-linux.tar.gz" && asset.size == 5 && asset.version == "1.2.3",
            "manifest asset fields");
    require(throws([] { update::parse_manifest("[]"); }), "manifest must be an object");
    require(throws([] { update::parse_manifest(R"({"version":"1.2.3"})"); }), "manifest must list assets");

    const fs::path archive = scratch / "hyperagent-x64-linux.tar.gz";
    write_text(archive, "hello");
    update::verify_archive(archive, asset);
    update::Asset wrong = asset;
    wrong.sha256 = std::string(64, '0');
    require(throws([&] { update::verify_archive(archive, wrong); }), "sha mismatch aborts");
    update::Asset resized = asset;
    resized.size = 6;
    require(throws([&] { update::verify_archive(archive, resized); }), "size mismatch aborts");

    const fs::path root = scratch / "install";
    update::switch_version(staged_tree(scratch, "1.2.3"), root, "1.2.3");
    require(fs::read_symlink(root / "current") == root / "versions" / "1.2.3", "current links the first version");
    require(fs::exists(root / "current" / "bin" / "hyperagent"), "current reaches the binary");

    update::switch_version(staged_tree(scratch, "1.2.4"), root, "1.2.4");
    require(fs::read_symlink(root / "current") == root / "versions" / "1.2.4", "current moves to the second version");
    require(fs::is_symlink(root / "previous-1.2.3"), "previous version is linked");
    require(fs::exists(root / "versions" / "1.2.3"), "previous version is kept");

    update::switch_version(staged_tree(scratch, "1.2.5"), root, "1.2.5");
    require(!fs::exists(root / "versions" / "1.2.3"), "older version is pruned");
    require(!fs::is_symlink(root / "previous-1.2.3"), "stale previous link is pruned");
    require(fs::exists(root / "versions" / "1.2.4"), "previous of the last switch is kept");

    fs::remove_all(scratch);
    std::cout << "UPDATE_UNIT_OK\n";
    return 0;
}
