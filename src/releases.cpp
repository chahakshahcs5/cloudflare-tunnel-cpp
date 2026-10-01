#include "cloudflared/releases.hpp"
#include <unordered_map>

namespace cloudflared {

namespace {

// Official SHA256 checksums from chahakshahcs5/Cloudflared bin.json
const std::unordered_map<std::string, std::string> CHECKSUMS_CLOUDFLARED = {
    { "cloudflared-windows-amd64.dll",   "d4f87009251fc952606dba6d8ad54f4a003ec290f93f37593c38badd1a60e745" },
    { "cloudflared-windows-386.dll",     "e9d74bcee3f9defbaebcffddcecca18cccb525340dbf49e7593528ee2b04ed43" },
    { "cloudflared-linux-amd64.so",      "a679b4357dc101a5e67918ccec041c5148352849d0add33bbf4e8b1d2f8edc02" },
    { "cloudflared-linux-arm64.so",      "bf19f3c419f8a594134f9e79fac8bb93bd372f91ff01b34d93dcfcfdc69884b3" },
    { "cloudflared-linux-arm.so",        "e4116910c22cd74298bd7f004f5147c74668f53a6c94500681f49fe8a4c53850" },
    { "cloudflared-linux-386.so",        "04a652d4ff5dfa8e2874d29de3abf850bec88f4379a7c2bdc385b8acb205e620" },
    { "cloudflared-android-arm64.so",    "f97fb6c074cb3b8e7a2104211c99fb617afed4439719415a03763a50501b4c32" },
    { "cloudflared-android-arm.so",      "e4ec9ee4c5c3b33b66d30137130c0735654ed675d9eca4f644e8de94e8b97970" },
    { "cloudflared-android-amd64.so",    "fdff7f4dea3ce1f66877d692b40ff74c676bc2bf5d15fa2f3d622dc388a17d10" },
    { "cloudflared-darwin-arm64.dylib",  "7483e4b1dc740a3e9b48e66aa8cc2015645feaf54ea4bfd62e858d4019b9a86f" },
    { "cloudflared-darwin-amd64.dylib",  "753ec3a360402b93728e193bd92889e7fb4014c3c829a38dc28cdafdc50aea57" }
};

} // namespace

ReleaseAsset get_release_asset(const PlatformInfo& platform, const std::string& version) {
    ReleaseAsset asset;
    asset.version = version;
    asset.filename = platform.asset_filename;

    std::string platform_tag = platform.os_name + "-" + platform.arch_name;
    asset.download_url = "https://raw.githubusercontent.com/chahakshahcs5/Cloudflared/main/binaries/" +
                         platform_tag + "/" + platform.asset_filename;

    auto it = CHECKSUMS_CLOUDFLARED.find(platform.asset_filename);
    if (it != CHECKSUMS_CLOUDFLARED.end()) {
        asset.sha256 = it->second;
    }

    return asset;
}

} // namespace cloudflared
