// Private decoding helpers. Include this file inside the PBF implementation namespace.

uint32_t string_id(uint64_t value)
{
    require(value <= UINT32_MAX, "PBF string ID exceeds 32 bits");
    return static_cast<uint32_t>(value);
}

size_t value_count(const field_t& field)
{
    if (field.wire == 0) return 1;
    auto bytes = field.message();
    size_t count = 0;
    while (!bytes.empty())
    {
        // Each eight-byte range starts and ends at a varint boundary.
        if (bytes.size() >= 8 && (bytes[7] & 0x80) == 0)
        {
            uint64_t word;
            std::memcpy(&word, bytes.data(), sizeof(word));
            count += std::popcount(~word & UINT64_C(0x8080808080808080));
            bytes = bytes.subspan(8);
        }
        else
        {
            reader_t reader(bytes);
            (void)reader.varint();
            bytes = reader.remaining();
            ++count;
        }
    }
    return count;
}

struct columns_t
{
    std::vector<int64_t> ids, latitudes, longitudes, refs, member_ids;
    std::vector<uint32_t> tag_offsets, ref_offsets, location_offsets, member_offsets, roles;
    std::vector<pbf_tag_ids_t> tags;
    std::vector<pbf_location_t> locations;
    std::vector<uint8_t> locations_present;
    std::vector<pbf_member_type_t> types;
    std::vector<pbf_metadata_t> info;
    size_t count = 0;

    void clear()
    {
        count = 0;
        ids.clear();
        latitudes.clear();
        longitudes.clear();
        refs.clear();
        member_ids.clear();
        tag_offsets.clear();
        ref_offsets.clear();
        location_offsets.clear();
        member_offsets.clear();
        roles.clear();
        tags.clear();
        locations.clear();
        locations_present.clear();
        types.clear();
        info.clear();
    }
    static uint32_t offset(size_t size)
    {
        require(size <= UINT32_MAX, "PBF output list exceeds 32 bits");
        return static_cast<uint32_t>(size);
    }
    static void end_list(std::vector<uint32_t>& offsets, size_t size)
    {
        if (offsets.empty()) offsets.push_back(0);
        offsets.push_back(offset(size));
    }
    static void read_info(bytes_t bytes, pbf_metadata_t& result)
    {
        reader_t reader(bytes);
        while (!reader.empty())
        {
            const auto field = reader.next();
            switch (field.number)
            {
                case 1:
                    result.version = narrow(signed_integer(field.integer()));
                    break;
                case 2:
                    result.raw_timestamp = signed_integer(field.integer());
                    break;
                case 3:
                    result.changeset = signed_integer(field.integer());
                    break;
                case 4:
                    result.uid = narrow(signed_integer(field.integer()));
                    break;
                case 5:
                    result.user_sid = string_id(field.integer());
                    break;
                case 6:
                    result.visible = field.integer() != 0;
                    break;
                default:
                    continue;
            }
            result.present |= uint8_t(1u << (field.number - 1));
        }
    }
    void ordinary(bytes_t bytes, unsigned kind, const pbf_entity_options_t& options, bool locations_allowed)
    {
        const bool nodes = kind == 1, ways = kind == 3;
        const auto n = options.nodes.value_or(pbf_node_options_t{});
        const auto w = options.ways.value_or(pbf_way_options_t{});
        const auto r = options.relations.value_or(pbf_relation_options_t{});
        const bool want_tags = nodes ? n.tags : ways ? w.tags : r.tags;
        const bool want_info = nodes ? n.metadata : ways ? w.metadata : r.metadata;
        const bool want_members = !nodes && !ways && (r.member_ids || r.member_types || r.member_roles);
        const size_t tag_start = tags.size(), ref_start = refs.size(), location_start = locations.size();
        size_t keys = 0, values = 0, reference_count = 0, latitude_count = 0, longitude_count = 0;
        size_t role_count = 0, id_count = 0, type_count = 0;
        int64_t id = 0, lat = 0, lon = 0, ref = 0, member_id = 0;
        unsigned required = 0, location_fields = 0;
        pbf_metadata_t meta;
        reader_t reader(bytes);
        while (!reader.empty())
        {
            const auto field = reader.next();
            switch (field.number)
            {
                case 1:
                    id = nodes ? zigzag(field.integer()) : signed_integer(field.integer());
                    required |= 1;
                    break;
                case 2:
                case 3:
                    if (want_tags)
                        repeated(field, [&](uint64_t value) {
                            auto& tag = column_item(tags, tag_start + (field.number == 2 ? keys++ : values++));
                            if (field.number == 2)
                                tag.key = string_id(value);
                            else
                                tag.value = string_id(value);
                        });
                    break;
                case 4:
                    if (want_info) read_info(field.message(), meta);
                    break;
                case 8:
                    if (nodes)
                    {
                        if (n.latitude) lat = zigzag(field.integer());
                        required |= 2;
                    }
                    else if (ways)
                    {
                        if (w.node_refs)
                            repeated(field, [&](uint64_t value) {
                                refs.push_back(ref = add_delta(ref, zigzag(value)));
                                ++reference_count;
                            });
                        else if (w.node_locations)
                            reference_count += value_count(field);
                    }
                    else if (want_members)
                    {
                        if (r.member_roles)
                            repeated(field, [&](uint64_t value) {
                                roles.push_back(static_cast<uint32_t>(narrow(signed_integer(value))));
                                ++role_count;
                            });
                        else
                            role_count += value_count(field);
                    }
                    break;
                case 9:
                    if (nodes)
                    {
                        if (n.longitude) lon = zigzag(field.integer());
                        required |= 4;
                    }
                    else if (ways && w.node_locations)
                    {
                        location_fields |= 1;
                        repeated(field, [&](uint64_t value) {
                            column_item(locations, location_start + latitude_count++).raw_latitude = lat = add_delta(
                                lat, zigzag(value));
                        });
                    }
                    else if (!ways && want_members)
                    {
                        if (r.member_ids)
                            repeated(field, [&](uint64_t value) {
                                member_ids.push_back(member_id = add_delta(member_id, zigzag(value)));
                                ++id_count;
                            });
                        else
                            id_count += value_count(field);
                    }
                    break;
                case 10:
                    if (ways && w.node_locations)
                    {
                        location_fields |= 2;
                        repeated(field, [&](uint64_t value) {
                            column_item(locations, location_start + longitude_count++).raw_longitude = lon = add_delta(
                                lon, zigzag(value));
                        });
                    }
                    else if (!nodes && !ways && want_members)
                    {
                        if (r.member_types)
                            repeated(field, [&](uint64_t value) {
                                require(value <= 2, "Invalid PBF member type");
                                types.push_back(static_cast<pbf_member_type_t>(value));
                                ++type_count;
                            });
                        else
                            type_count += value_count(field);
                    }
                    break;
                default:
                    break;
            }
        }
        require(required == (nodes ? 7u : 1u), "Missing required entity field");
        if (!nodes || n.id) ids.push_back(id);
        if (nodes && n.latitude) latitudes.push_back(lat);
        if (nodes && n.longitude) longitudes.push_back(lon);
        if (want_tags)
        {
            require(keys == values, "PBF tag arrays have different sizes");
            end_list(tag_offsets, tags.size());
        }
        if (want_info) info.push_back(meta);
        if (ways && w.node_refs) end_list(ref_offsets, ref_start + reference_count);
        if (ways && w.node_locations)
        {
            require(!location_fields || (location_fields == 3 && locations_allowed &&
                                         latitude_count == reference_count && longitude_count == reference_count),
                    "Invalid PBF way locations");
            end_list(location_offsets, locations.size());
            locations_present.push_back(location_fields != 0);
        }
        if (want_members)
        {
            require(role_count == id_count && id_count == type_count, "Invalid PBF member array sizes");
            const auto previous = member_offsets.empty() ? 0 : member_offsets.back();
            end_list(member_offsets, size_t(previous) + id_count);
        }
        ++count;
    }
    static void deltas(const std::vector<field_t>& fields, std::vector<int64_t>& output)
    {
        int64_t value = 0;
        for (const auto& field : fields)
            repeated(field, [&](uint64_t encoded) { output.push_back(value = add_delta(value, zigzag(encoded))); });
    }
    bool packed_info(const std::array<std::vector<field_t>, 10>& fields)
    {
        std::array<bytes_t, 6> encoded;
        uint8_t present = 0;
        for (size_t i = 0; i < 6; ++i)
        {
            if (fields[i + 4].empty()) continue;
            if (fields[i + 4].size() != 1 || fields[i + 4][0].wire != 2) return false;
            encoded[i] = fields[i + 4][0].bytes;
            if (!encoded[i].empty()) present |= uint8_t(1u << i);
        }
        if (!present)
        {
            info.resize(count);
            return true;
        }
        reader_t version(encoded[0]), timestamp(encoded[1]), changeset(encoded[2]), uid(encoded[3]), user(encoded[4]),
            visible(encoded[5]);
        int64_t time = 0, change = 0, user_id = 0, user_name = 0;
        if (count > info.capacity()) info.reserve(std::max(count, info.capacity() * 2));
        for (size_t i = 0; i < count; ++i)
        {
            pbf_metadata_t item;
            item.present = present;
            if (present & 1) item.version = narrow(signed_integer(version.varint()));
            if (present & 2) item.raw_timestamp = time = add_delta(time, zigzag(timestamp.varint()));
            if (present & 4) item.changeset = change = add_delta(change, zigzag(changeset.varint()));
            if (present & 8) item.uid = narrow(user_id = add_delta(user_id, zigzag(uid.varint())));
            if (present & 16)
                item.user_sid = static_cast<uint32_t>(narrow(user_name = add_delta(user_name, zigzag(user.varint()))));
            if (present & 32) item.visible = visible.varint() != 0;
            info.push_back(item);
        }
        require(
            version.empty() && timestamp.empty() && changeset.empty() && uid.empty() && user.empty() && visible.empty(),
            "Extra dense metadata value");
        return true;
    }
    void dense(const std::array<std::vector<field_t>, 10>& fields, pbf_node_options_t options)
    {
        if (options.id)
        {
            deltas(fields[0], ids);
            count = ids.size();
        }
        else
            for (const auto& field : fields[0]) count += value_count(field);
        if (options.latitude)
        {
            deltas(fields[1], latitudes);
            require(latitudes.size() == count, "Invalid dense latitude count");
        }
        if (options.longitude)
        {
            deltas(fields[2], longitudes);
            require(longitudes.size() == count, "Invalid dense longitude count");
        }
        if (options.metadata && !packed_info(fields))
        {
            info.resize(count);
            for (size_t column = 4; column < 10; ++column)
            {
                size_t index = 0;
                int64_t accumulator = 0;
                for (const auto& field : fields[column])
                    repeated(field, [&](uint64_t encoded) {
                        require(index < count, "Extra dense metadata value");
                        auto& item = info[index++];
                        item.present |= uint8_t(1u << (column - 4));
                        switch (column)
                        {
                            case 4:
                                item.version = narrow(signed_integer(encoded));
                                break;
                            case 5:
                                item.raw_timestamp = accumulator = add_delta(accumulator, zigzag(encoded));
                                break;
                            case 6:
                                item.changeset = accumulator = add_delta(accumulator, zigzag(encoded));
                                break;
                            case 7:
                                item.uid = narrow(accumulator = add_delta(accumulator, zigzag(encoded)));
                                break;
                            case 8:
                                item.user_sid = static_cast<uint32_t>(
                                    narrow(accumulator = add_delta(accumulator, zigzag(encoded))));
                                break;
                            case 9:
                                item.visible = encoded != 0;
                                break;
                        }
                    });
                require(index == count || index == 0, "Missing dense metadata values");
            }
        }
        if (options.tags)
        {
            tag_offsets.push_back(0);
            bool has_key = false, has_values = false;
            uint32_t key = 0;
            for (const auto& field : fields[3])
                repeated(field, [&](uint64_t value) {
                    has_values = true;
                    require(tag_offsets.size() <= count, "Extra dense tags");
                    if (has_key)
                    {
                        tags.push_back({key, static_cast<uint32_t>(narrow(signed_integer(value)))});
                        has_key = false;
                    }
                    else if (value == 0)
                        tag_offsets.push_back(offset(tags.size()));
                    else
                    {
                        key = static_cast<uint32_t>(narrow(signed_integer(value)));
                        has_key = true;
                    }
                });
            require(!has_key, "Missing dense tag value");
            if (!has_values) tag_offsets.resize(count + 1, 0);
            require(tag_offsets.size() == count + 1, "Missing dense tag delimiters");
        }
    }
    void empty_lists(unsigned kind, const pbf_entity_options_t& options)
    {
        if ((kind <= 2 && options.nodes && options.nodes->tags) || (kind == 3 && options.ways && options.ways->tags) ||
            (kind == 4 && options.relations && options.relations->tags))
            if (tag_offsets.empty()) tag_offsets.push_back(0);
        if (kind == 3 && options.ways)
        {
            if (options.ways->node_refs && ref_offsets.empty()) ref_offsets.push_back(0);
            if (options.ways->node_locations && location_offsets.empty()) location_offsets.push_back(0);
        }
        if (kind == 4 && options.relations &&
            (options.relations->member_ids || options.relations->member_types || options.relations->member_roles))
            if (member_offsets.empty()) member_offsets.push_back(0);
    }
    pbf_group_batch_t batch(size_t block_index,
                            size_t group_index,
                            unsigned kind,
                            pbf_parameters_t parameters,
                            const pbf_entity_options_t& options)
    {
        pbf_group_batch_t result;
        result.block_index = block_index;
        result.group_index = group_index;
        result.kind = static_cast<pbf_group_kind_t>(kind);
        const pbf_batch_context_t context{block_index, group_index, count, parameters};
        if (kind <= 2 && options.nodes)
        {
            auto& out = result.nodes;
            static_cast<pbf_batch_context_t&>(out) = context;
            out.fields = *options.nodes;
            out.ids = ids;
            out.raw_latitudes = latitudes;
            out.raw_longitudes = longitudes;
            out.tags = {tag_offsets, tags};
            out.metadata = info;
        }
        if (kind == 3 && options.ways)
        {
            auto& out = result.ways;
            static_cast<pbf_batch_context_t&>(out) = context;
            out.fields = *options.ways;
            out.ids = ids;
            out.tags = {tag_offsets, tags};
            out.node_refs = {ref_offsets, refs};
            out.node_locations = {location_offsets, locations};
            out.locations_present = locations_present;
            out.metadata = info;
        }
        if (kind == 4 && options.relations)
        {
            auto& out = result.relations;
            static_cast<pbf_batch_context_t&>(out) = context;
            out.fields = *options.relations;
            out.ids = ids;
            out.tags = {tag_offsets, tags};
            out.member_offsets = member_offsets;
            out.member_ids = member_ids;
            out.member_types = types;
            out.member_roles = roles;
            out.metadata = info;
        }
        return result;
    }
};
