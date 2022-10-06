// Copyright 2021-2022 Stefan Karschti
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <inputosm/inputosm.h>

#include <cstdio>
#include <cstdlib>
#include <numeric>
#include <map>
#include <cstring>
#include <vector>
#include <algorithm>
#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <unordered_map>
#include <thread>

#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/mman.h>

bool write_file(const char* filename, std::vector<std::string> &lines)
{
    size_t file_size = 0;
    for(const auto &s: lines)
    {
        file_size += s.length();
    }
    std::cout << "file size: " << file_size << " bytes\n";
    if(file_size > 0)
    {
        int fd;
        if((fd = open(filename, O_CREAT|O_WRONLY|O_TRUNC, 0644)) == -1)
        {
            perror("create");
            return false;
        }
        if(file_size - 1 == lseek64(fd, file_size - 1, SEEK_SET))
        {
            write(fd, &file_size, 1);
        }
        close(fd);
        if((fd = open(filename, O_RDWR)) == -1)
        {
            perror("open");
            return false;
        }
        uint8_t* file_data = (uint8_t*)mmap((caddr_t)0, file_size, PROT_WRITE, MAP_SHARED, fd, 0);
        if((caddr_t)file_data == (caddr_t)(-1))
        {
            perror("mmap");
        }
        close(fd);
        if((caddr_t)file_data != (caddr_t)(-1))
        {
            // compute offsets
            std::vector<size_t> offsets;
            for(size_t offset{0}; auto &s: lines)
            {
                offsets.push_back(offset);
                offset += s.length();
            }
            // write threaded
            auto work = [&lines, &offsets, file_data](int index){
                auto &off = offsets[index];
                memcpy(file_data + offsets[index], lines[index].data(), lines[index].length());
                std::cout << ".";
                std::cout.flush();
            };
            // spawn workers
            size_t thread_count = offsets.size();
            std::vector<std::thread> worker_threads(thread_count);
            for(size_t index{0}; index < thread_count; index++)
            {
                worker_threads[index] = std::thread(work, index);
            }
            // wait for them to finish
            for(auto& th: worker_threads)
            {
                if(th.joinable())
                    th.join();
            }
            std::cout << "\n";
            // unmap
            if(munmap(file_data, file_size) == -1)
            {
                perror("munmap");
            }
        }
    }
    return true;
}

int main(int argc, char **argv)
{
    if(argc < 2)
    {
        printf("Usage %s <path-to-pbf>\n", argv[0]);
        return EXIT_FAILURE;
    }
    const char* path = argv[1];
    input_osm::set_max_thread_count();
    printf("running on %zu threads\n", input_osm::thread_count());
    std::vector<std::string> lines(input_osm::thread_count());
    struct pos
    {
        int32_t lat = 0;
        int32_t lon = 0;
    };
    static_assert(sizeof(pos) == 8);
    std::vector<std::unordered_map<int64_t, pos>> node_pos_thread(input_osm::thread_count());

    // nodes
    std::cout << "extracting nodes...\n";
    if(!input_osm::input_file(
            path,
            true,
            [&lines, &node_pos_thread](input_osm::span_t<input_osm::node_t> node_list) noexcept -> bool
            {
                std::stringstream ss;
                for(auto &n: node_list)
                {
                    node_pos_thread[input_osm::thread_index][n.id].lat = n.raw_latitude;
                    node_pos_thread[input_osm::thread_index][n.id].lon = n.raw_longitude;
                    ss << n.id << ";0;0;" << n.timestamp << ";" << n.changeset << ";'";
                    for(auto itag = n.tags.begin(); itag != n.tags.end(); ++itag)
                    {
                        ss << "\"" << itag->key << "\"=>\"" << itag->value << "\"";
                        if(itag < n.tags.end() - 1)
                            ss << ",";
                    }
                    ss << "';POINT(" << std::fixed << std::setprecision(7) << n.raw_latitude / 10'000'000.0 << " " << n.raw_longitude / 10'000'000.0 << ")\n";
                }
                lines[input_osm::thread_index] += ss.str();
                return true; 
            },
            nullptr,
            nullptr
            ))
    {
        printf("Error while processing pbf\n");
        return EXIT_FAILURE;
    }
    std::cout << "writing nodes csv...\n";
    write_file("nodes.csv", lines);
    for(auto &line: lines)
    {
        line.clear();
        line.shrink_to_fit();
    }
    pos no_node{0, 0};
    auto node_pos = [&node_pos_thread, &no_node](int64_t id)->pos& {
        for(auto &npt: node_pos_thread)
            if(auto it = npt.find(id); it != npt.end())
                return it->second;
        return no_node;
    };

    // ways
    std::cout << "extracting ways and relations...\n";
    std::vector<std::string> lines_way_node(input_osm::thread_count());
    std::vector<std::string> lines_relations(input_osm::thread_count());
    std::vector<std::string> lines_relation_members(input_osm::thread_count());
    if(!input_osm::input_file(
            path,
            true,
            nullptr,
            [&lines, &node_pos, &lines_way_node](input_osm::span_t<input_osm::way_t> way_list) noexcept -> bool
            {
                std::stringstream ss;
                std::stringstream ss_way_node;
                for(auto &way: way_list)
                {
                    ss << way.id << ";0;0;" << way.timestamp << ";" << way.changeset << ";'";
                    for(auto itag = way.tags.begin(); itag != way.tags.end(); ++itag)
                    {
                        ss << "\"" << itag->key << "\"=>\"" << itag->value << "\"";
                        if(itag < way.tags.end() - 1)
                            ss << ",";
                    }
                    ss << "';{";
                    size_t sequence_id = 0;
                    for(auto inode = way.node_refs.begin(); inode != way.node_refs.end(); ++inode)
                    {
                        ss_way_node << way.id << ";" << *inode << ";" << sequence_id++ << "\n";
                        ss << *inode;
                        if(inode < way.node_refs.end() - 1)
                            ss << ",";
                    }
                    ss << "};";
                    ss << "BBOX();";
                    ss << "LINESTRING(";
                    for(auto inode = way.node_refs.begin(); inode != way.node_refs.end(); ++inode)
                    {
                        auto &n = node_pos(*inode);
                        ss << std::fixed << std::setprecision(7) << n.lat / 10'000'000.0 << " " << n.lon / 10'000'000.0;
                        if(inode < way.node_refs.end() - 1)
                            ss << ",";
                    }
                    ss << ")\n";
                }
                lines[input_osm::thread_index] += ss.str();
                lines_way_node[input_osm::thread_index] += ss_way_node.str();
                return true; 
            },
            [&lines_relations, &lines_relation_members](input_osm::span_t<input_osm::relation_t> relation_list) noexcept -> bool
            {
                std::stringstream ss;
                std::stringstream ss_members;
                for(auto &relation: relation_list)
                {
                    ss << relation.id << ";0;0;" << relation.timestamp << ";" << relation.changeset << ";'";
                    for(auto itag = relation.tags.begin(); itag != relation.tags.end(); ++itag)
                    {
                        ss << "\"" << itag->key << "\"=>\"" << itag->value << "\"";
                        if(itag < relation.tags.end() - 1)
                            ss << ",";
                    }
                    ss << "'\n";

                    size_t sequence_id = 0;
                    for(auto imember = relation.members.begin(); imember != relation.members.end(); ++imember)
                    {
                        const char types[3] = {'N', 'W', 'R'};
                        ss_members << relation.id << ";" << imember->id << ";" << types[imember->type] << ";\"" << imember->role << "\";" << sequence_id++ << "\n";
                    }
                }
                lines_relations[input_osm::thread_index] += ss.str();
                lines_relation_members[input_osm::thread_index] += ss_members.str();
                return true;
            }
            ))
    {
        printf("Error while processing pbf\n");
        return EXIT_FAILURE;
    }
    std::cout << "writing ways csv...\n";
    write_file("ways.csv", lines);

    std::cout << "writing way nodes csv...\n";
    write_file("way_node.csv", lines_way_node);

    std::cout << "writing relations csv...\n";
    write_file("relations.csv", lines_relations);

    std::cout << "writing relation members csv...\n";
    write_file("relation_members.csv", lines_relation_members);

    std::cout << "done.\n";

    return EXIT_SUCCESS;
}