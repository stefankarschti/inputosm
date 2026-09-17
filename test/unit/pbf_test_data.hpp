#pragma once

#include <bit>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>
#include <libdeflate.h>
#include <unistd.h>

namespace pbf_test
{
inline std::string varint(uint64_t value)
{
    std::string result;
    while (value >= 128)
    {
        result += static_cast<char>((value & 127) | 128);
        value >>= 7;
    }
    result += static_cast<char>(value);
    return result;
}
inline uint64_t sint(int64_t value)
{
    const auto bits = std::bit_cast<uint64_t>(value);
    return (bits << 1) ^ (uint64_t{0} - (bits >> 63));
}
inline std::string integer(uint32_t number, uint64_t value)
{
    return varint(uint64_t(number) << 3) + varint(value);
}
inline std::string message(uint32_t number, std::string_view value)
{
    return varint((uint64_t(number) << 3) | 2) + varint(value.size()) + std::string(value);
}
inline std::string packed(uint32_t number, std::initializer_list<uint64_t> values)
{
    std::string bytes;
    for (auto value : values) bytes += varint(value);
    return message(number, bytes);
}
inline std::string prefix(uint32_t value)
{
    std::string result;
    for (int shift : {24, 16, 8, 0}) result += static_cast<char>((value >> shift) & 255);
    return result;
}
inline std::string file_block(std::string_view type, std::string_view blob)
{
    const auto header = message(1, type) + integer(3, blob.size());
    return prefix(static_cast<uint32_t>(header.size())) + header + std::string(blob);
}
inline std::string raw_block(std::string_view type, std::string_view payload)
{
    return file_block(type, message(1, payload));
}
inline std::string compressed(std::string_view payload)
{
    const std::unique_ptr<libdeflate_compressor, decltype(&libdeflate_free_compressor)> compressor(
        libdeflate_alloc_compressor(6), libdeflate_free_compressor);
    if (!compressor) throw std::runtime_error("Cannot allocate fixture compressor");
    std::string result(libdeflate_zlib_compress_bound(compressor.get(), payload.size()), '\0');
    const auto size = libdeflate_zlib_compress(
        compressor.get(), payload.data(), payload.size(), result.data(), result.size());
    if (size == 0) throw std::runtime_error("Fixture compression failed");
    result.resize(size);
    return result;
}
inline std::string zlib_block(std::string_view payload)
{
    return file_block("OSMData", integer(2, payload.size()) + message(3, compressed(payload)));
}
inline std::string header(std::string_view feature = "OsmSchema-V0.6")
{
    return raw_block("OSMHeader", message(4, feature) + message(4, "DenseNodes"));
}
inline std::string table(std::initializer_list<std::string_view> strings = {"", "name", "value"})
{
    std::string bytes;
    for (auto string : strings) bytes += message(1, string);
    return message(1, bytes);
}
inline std::string node(int64_t id = 1)
{
    return integer(1, sint(id)) + integer(8, sint(-100)) + integer(9, sint(200));
}
inline std::string primitive(std::string_view group = {})
{
    return table() + message(2, group);
}

class files_t
{
public:
    files_t()
    {
        auto pattern = (std::filesystem::temp_directory_path() / "inputosm-pbf-XXXXXX").string();
        if (!mkdtemp(pattern.data())) throw std::runtime_error("Cannot create test directory");
        directory = pattern;
    }
    ~files_t()
    {
        std::error_code error;
        std::filesystem::remove_all(directory, error);
    }
    files_t(const files_t&) = delete;
    files_t& operator=(const files_t&) = delete;
    std::string write(std::string_view bytes, std::string_view extension = ".pbf")
    {
        const auto path = directory / (std::to_string(next++) + std::string(extension));
        std::ofstream stream(path, std::ios::binary);
        stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        stream.close();
        if (!stream) throw std::runtime_error("Cannot write PBF test input");
        return path.string();
    }

private:
    std::filesystem::path directory;
    size_t next = 0;
};
} // namespace pbf_test
