#ifndef INPUTOSM_H
#define INPUTOSM_H

#include <cstdint>
#include <functional>

namespace slim_osm {
struct tag_t
{
    const char* key = nullptr;
    const char* value = nullptr;
};

struct node_t
{
    int64_t id = 0;
    int64_t raw_latitude = 0;
    int64_t raw_longitude = 0;
    double longitude = 0;
    double latitude = 0;
    size_t tag_count = 0;
    tag_t *tags = nullptr;
    int32_t version = 0;
    int32_t timestamp = 0;
    int32_t changeset = 0;
};

struct way_t
{
    int64_t id = 0;
    size_t node_ref_count = 0;
    int64_t *node_refs = nullptr;
    size_t tag_count = 0;
    tag_t *tags = nullptr;
    int32_t version = 0;
    int32_t timestamp = 0;
    int32_t changeset = 0;
};

struct relation_member_t
{
    uint8_t type = 0;
    int64_t id = 0;
    const char* role = nullptr;
};

struct relation_t
{
    int64_t id = 0;
    size_t member_count = 0;
    relation_member_t *members = nullptr;
    size_t tag_count = 0;
    tag_t *tags = nullptr;
    int32_t version = 0;
    int32_t timestamp = 0;
    int32_t changeset = 0;
};

enum class file_type_t
{    
    pbf,
    xml
};

enum class mode_t
{
    bulk,
    create,
    modify,
    destroy
};

extern mode_t osc_mode;

bool input_file(const char* filename,
                bool decode_metadata,
                bool decode_node_coord,
                std::function<bool(const node_t&)> node_handler,
                std::function<bool(const way_t&)> way_handler,
                std::function<bool(const relation_t&)> relation_handler);

} // namespace

#endif // !INPUTOSM_H
