add_executable(pbf_benchmark pbf_benchmark.cpp)
target_link_libraries(pbf_benchmark PRIVATE inputosm::inputosm Threads::Threads)
