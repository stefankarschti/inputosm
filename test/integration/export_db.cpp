#include <inputosm/inputosm.h>

#include <fmt/format.h>
#include <cstdio>
#include <cstdint>
#include <numeric>
#include <vector>
#include <string>
#include <cstring>
#include <cerrno>

#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <unistd.h>

bool open_and_map(const char* filename, caddr_t& file_data, uint64_t& file_size)
{
    struct stat mmapstat;
    if (::stat(filename, &mmapstat) == -1)
    {
        fmt::print(stderr, "{}: {}\n", "stat", std::strerror(errno));
        return false;
    }
    file_size = mmapstat.st_size;
    if (0 == file_size)
    {
        file_data = nullptr;
        return true;
    }
    int fd;
    if ((fd = open(filename, O_RDONLY)) == -1)
    {
        fmt::print(stderr, "{}: {}\n", "open", std::strerror(errno));
        return false;
    }
    file_data = (caddr_t)mmap((caddr_t)0, file_size, PROT_READ, MAP_SHARED, fd, 0);
    if (file_data == (caddr_t)(-1))
    {
        fmt::print(stderr, "{}: {}\n", "mmap", std::strerror(errno));
    }
    close(fd);
    return (file_data != (caddr_t)(-1));
}

bool unmap_and_close(caddr_t file_data, uint64_t file_size)
{
    int result = munmap(file_data, file_size);
    if (result == -1)
    {
        fmt::print(stderr, "{}: {}\n", "munmap", std::strerror(errno));
    }
    return result != -1;
}

bool close_files(std::vector<int>& files)
{
    bool result = true;
    for (auto& fd : files)
    {
        if (-1 == ::close(fd))
        {
            fmt::print(stderr, "{}: {}\n", "close", std::strerror(errno));
            result = false;
        }
        else
        {
            fd = -1;
        }
    }
    return result;
}

bool concatenate_and_remove_files(const char* root_filename, size_t file_count)
{
    int foutput = ::open(root_filename, O_CREAT | O_WRONLY | O_TRUNC, 0644);
    if (foutput == -1)
    {
        fmt::print(stderr, "{}: {}\n", "open", std::strerror(errno));
        return false;
    }
    bool result = true;
    for (size_t i = 0; i < file_count; ++i)
    {
        std::string filename{root_filename + std::to_string(i)};
        caddr_t file_data{nullptr};
        uint64_t file_size{0};
        if (open_and_map(filename.c_str(), file_data, file_size))
        {
            if (file_size > 0)
            {
                ::write(foutput, file_data, file_size);
                unmap_and_close(file_data, file_size);
            }
            ::unlink(filename.c_str());
        }
        else
            result = false;
    }
    ::close(foutput);
    if (!result) ::unlink(root_filename);
    return result;
}

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        fmt::print(stderr, "Usage{}<path-to-pbf> [read-metadata]\n", argv[0]);
        return EXIT_FAILURE;
    }
    const char* path = argv[1];
    fmt::print("importing {}\n", path);
    input_osm::set_max_thread_count();

    // File descriptors for nodes, ways, and relations.
    std::vector<int> node_files(input_osm::thread_count(), -1);
    std::vector<int> way_files(input_osm::thread_count(), -1);
    std::vector<int> relation_files(input_osm::thread_count(), -1);
    for (size_t i = 0; i < input_osm::thread_count(); ++i)
    {
        using namespace std::string_literals;
        node_files[i] = ::open(("node"s + std::to_string(i)).c_str(), O_CREAT | O_WRONLY | O_TRUNC, 0644);
        way_files[i] = ::open(("way"s + std::to_string(i)).c_str(), O_CREAT | O_WRONLY | O_TRUNC, 0644);
        relation_files[i] = ::open(("relation"s + std::to_string(i)).c_str(), O_CREAT | O_WRONLY | O_TRUNC, 0644);
    }
    std::vector<uint64_t> node_count(input_osm::thread_count(), 0);
    std::vector<uint64_t> way_count(input_osm::thread_count(), 0);
    std::vector<uint64_t> relation_count(input_osm::thread_count(), 0);

    if (!input_osm::input_file(
            path,
            true,
            [&node_count, &node_files](std::span<const input_osm::node_t> node_list) -> bool {
                node_count[input_osm::thread_index] += node_list.size();
                int fd = node_files[input_osm::thread_index];
                for (auto& n : node_list)
                {
                    ::write(fd, &n.id, sizeof(int64_t));
                    int32_t lat{static_cast<int32_t>(n.raw_latitude)};
                    int32_t lon{static_cast<int32_t>(n.raw_longitude)};
                    ::write(fd, &lat, sizeof(int32_t));
                    ::write(fd, &lon, sizeof(int32_t));
                    int16_t size = n.tags.size();
                    ::write(fd, &size, sizeof(int16_t));
                    for (auto& tag : n.tags)
                    {
                        if (!tag.key.empty()) ::write(fd, tag.key.data(), tag.key.size());
                        ::write(fd, "", 1);
                        if (!tag.value.empty()) ::write(fd, tag.value.data(), tag.value.size());
                        ::write(fd, "", 1);
                    }
                }
                return true;
            },
            [&way_count, &way_files](std::span<const input_osm::way_t> way_list) -> bool {
                way_count[input_osm::thread_index] += way_list.size();
                int fd = way_files[input_osm::thread_index];
                for (auto& w : way_list)
                {
                    ::write(fd, &w.id, sizeof(int64_t));
                    int16_t size = w.node_refs.size();
                    ::write(fd, &size, sizeof(int16_t));
                    ::write(fd, w.node_refs.data(), w.node_refs.size() * sizeof(int64_t));
                    size = w.tags.size();
                    ::write(fd, &size, sizeof(int16_t));
                    for (auto& tag : w.tags)
                    {
                        if (!tag.key.empty()) ::write(fd, tag.key.data(), tag.key.size());
                        ::write(fd, "", 1);
                        if (!tag.value.empty()) ::write(fd, tag.value.data(), tag.value.size());
                        ::write(fd, "", 1);
                    }
                }
                return true;
            },
            [&relation_count, &relation_files](std::span<const input_osm::relation_t> relation_list) -> bool {
                relation_count[input_osm::thread_index] += relation_list.size();
                int fd = relation_files[input_osm::thread_index];
                for (auto& r : relation_list)
                {
                    ::write(fd, &r.id, sizeof(int64_t));
                    int16_t size = r.members.size();
                    ::write(fd, &size, sizeof(int16_t));
                    for (auto& m : r.members)
                    {
                        ::write(fd, &m.id, sizeof(int64_t));
                        if (!m.role.empty()) ::write(fd, m.role.data(), m.role.size());
                        ::write(fd, "", 1);
                        ::write(fd, &m.type, sizeof(int8_t));
                    }
                    size = r.tags.size();
                    ::write(fd, &size, sizeof(int16_t));
                    for (auto& tag : r.tags)
                    {
                        if (!tag.key.empty()) ::write(fd, tag.key.data(), tag.key.size());
                        ::write(fd, "", 1);
                        if (!tag.value.empty()) ::write(fd, tag.value.data(), tag.value.size());
                        ::write(fd, "", 1);
                    }
                }
                return true;
            }))
    {
        fmt::print(stderr, "Error while processing pbf\n");
        return EXIT_FAILURE;
    }

    // Write the data from each thread to the output files.
    close_files(node_files);
    concatenate_and_remove_files("node", node_files.size());
    close_files(way_files);
    concatenate_and_remove_files("way", way_files.size());
    close_files(relation_files);
    concatenate_and_remove_files("relation", relation_files.size());

    size_t num_nodes{std::accumulate(node_count.begin(), node_count.end(), 0LLU)};
    size_t num_ways{std::accumulate(way_count.begin(), way_count.end(), 0LLU)};
    size_t num_relations{std::accumulate(relation_count.begin(), relation_count.end(), 0LLU)};
    fmt::print("nodes: {}\n", fmt::group_digits(num_nodes));
    fmt::print("ways: {}\n", fmt::group_digits(num_ways));
    fmt::print("relations: {}\n", fmt::group_digits(num_relations));

    return EXIT_SUCCESS;
}
