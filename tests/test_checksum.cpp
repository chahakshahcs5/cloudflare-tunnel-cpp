#include "cloudflared/checksum.hpp"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>

void test_checksum_vectors() {
    std::cout << "[TEST] Running test_checksum_vectors..." << std::endl;

    // Standard NIST test vectors
    // 1. Empty string
    std::string empty_hash = cloudflared::compute_sha256_hex("");
    assert(empty_hash == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");

    // 2. "abc"
    std::string abc_hash = cloudflared::compute_sha256_hex("abc");
    assert(abc_hash == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");

    // 3. File streaming checksum
    std::filesystem::path temp_file = std::filesystem::temp_directory_path() / "test_sha256.tmp";
    {
        std::ofstream f(temp_file, std::ios::binary);
        f << "The quick brown fox jumps over the lazy dog";
    }

    std::string file_hash = cloudflared::compute_file_sha256_hex(temp_file);
    assert(file_hash == "d7a8fbb307d7809469ca9abced04a284bd4b613e7e999968f564b4f39b4cce82");

    // 4. Verification helper
    assert(cloudflared::verify_file_sha256(temp_file, "d7a8fbb307d7809469ca9abced04a284bd4b613e7e999968f564b4f39b4cce82"));
    assert(cloudflared::verify_file_sha256(temp_file, "D7A8FBB307D7809469CA9ABCED04A284BD4B613E7E999968F564B4F39B4CCE82")); // case-insensitive
    assert(!cloudflared::verify_file_sha256(temp_file, "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff")); // mismatch

    std::error_code ec;
    std::filesystem::remove(temp_file, ec);

    std::cout << "[PASS] Checksum verification tests passed!" << std::endl;
}

int main() {
    test_checksum_vectors();
    return 0;
}
