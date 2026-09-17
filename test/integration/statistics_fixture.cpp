#include "../unit/pbf_test_data.hpp"

#include <cstdio>

int main(int argc, char** argv)
{
    if (argc != 2) return 1;
    try
    {
        using namespace pbf_test;
        const auto one_tag = packed(2, {1}) + packed(3, {2});
        const auto two_tags = packed(2, {1, 3}) + packed(3, {2, 4});
        const auto timestamp = [](int64_t value) {
            return message(4, integer(2, value));
        };
        const auto strings = table({"", "name", "value", "source", "test"});
        const auto ordinary = message(1, node(10) + two_tags + timestamp(1000000000)) + message(1, node(11));
        const auto dense = packed(1, {sint(20), sint(1), sint(1)}) + packed(8, {0, 0, 0}) + packed(9, {0, 0, 0}) +
                           packed(10, {1, 2, 0, 0, 0}) + message(5, packed(2, {sint(4000000000LL), sint(-1), 0}));
        const auto way = [&](int64_t id, std::initializer_list<uint64_t> refs, std::string_view tags) {
            return integer(1, id) + packed(8, refs) + std::string(tags);
        };
        const auto relation = [&](int64_t id, size_t members, std::string_view tags) {
            std::string result = integer(1, id) + std::string(tags);
            for (size_t i = 0; i < members; ++i) result += integer(8, 0) + integer(9, sint(1)) + integer(10, 0);
            return result;
        };
        const auto first = strings + integer(18, 2000) + message(2, ordinary) + message(2, message(2, dense)) +
                           message(2, message(3, way(30, {2, 2}, one_tag) + timestamp(2200000000LL))) +
                           message(2, message(3, way(31, {2, 2, 2}, ""))) +
                           message(2, message(4, relation(40, 2, one_tag))) +
                           message(2, message(4, relation(41, 1, two_tags)));
        const auto second = strings + integer(18, 500) +
                            message(2,
                                    message(1, node(99) + one_tag + timestamp(6000000000LL)) + message(1, node(100))) +
                            message(2, message(3, way(90, {2}, two_tags) + timestamp(7000000000LL))) +
                            message(2, message(4, relation(91, 4, "") + timestamp(9400000001LL)));
        const auto bytes = header() + raw_block("OSMData", first) + zlib_block(second) + raw_block("Unused", "") +
                           raw_block("OSMData", strings + message(2, ""));
        std::ofstream output(argv[1], std::ios::binary);
        output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        output.close();
        return output ? 0 : 1;
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
