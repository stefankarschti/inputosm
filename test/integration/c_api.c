#include <inputosm/inputosm_c.h>

#include <stdio.h>
#include <stdlib.h>
#include <time.h>

const char* lvl_to_str(const inputosm_log_level_t lvl)
{
    switch (lvl)
    {
        case INPUTOSM_LOG_LEVEL_TRACE:
            return "TRC";
        case INPUTOSM_LOG_LEVEL_INFO:
            return "INF";
        case INPUTOSM_LOG_LEVEL_ERROR:
            return "ERR";
        default:
            break;
    }
    return "NON";
}

static void phony_log_callback(inputosm_log_level_t level, const char* message)
{
    const time_t t = time(NULL);
    char time_buf[100];
    size_t rc = strftime(time_buf, sizeof(time_buf), "%D %T", gmtime(&t));
    snprintf(time_buf + rc, sizeof(time_buf) - rc, ".%06ld UTC", t / 1000);
    printf("%s [%s]: %s\n", time_buf, lvl_to_str(level), message);
}

typedef struct
{
    uint64_t* nodes;
    uint64_t* ways;
    uint64_t* relations;
} Statistics;

static bool nodes_callback(void* user, const inputosm_node_t* nodes_list, size_t nodes_size)
{
    (void)nodes_list;
    Statistics* stats = (Statistics*)user;
    // printf("thread index: %lu\n", inputosm_thread_index());
    stats->nodes[inputosm_thread_index()] += nodes_size;
    return 1;
}

static bool ways_callback(void* user, const inputosm_way_t* ways_list, size_t ways_size)
{
    (void)ways_list;
    Statistics* stats = (Statistics*)user;
    // printf("thread index: %lu\n", inputosm_thread_index());
    stats->ways[inputosm_thread_index()] += ways_size;
    return 1;
}

static bool relations_callback(void* user, const inputosm_relation_t* relations_list, size_t relations_size)
{
    (void)relations_list;
    Statistics* stats = (Statistics*)user;
    // printf("thread index: %lu\n", inputosm_thread_index());
    stats->relations[inputosm_thread_index()] += relations_size;
    return 1;
}

static size_t align_up(size_t n, size_t alignment)
{
    return (n + (alignment - 1)) & ~(alignment - 1);
}

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        printf("Usage %s <path-to-pbf> [read-metadata]\n", argv[0]);
        return 1;
    }

    if (!inputosm_set_log_callback(phony_log_callback))
    {
        puts("Invalid log callback set!");
        return 1;
    }

    puts("Log callback installed successfully!");
    // const int log_level = INPUTOSM_LOG_LEVEL_TRACE;
    const int log_level = INPUTOSM_LOG_LEVEL_INFO;
    inputosm_set_log_level(log_level);
    printf("Log level set to: %d (%s)\n", log_level, lvl_to_str(log_level));

    inputosm_set_thread_count(8);
    printf("Thread count set to: %lu\n", inputosm_thread_count());

    inputosm_set_max_thread_count();
    printf("Thread count set to: %lu\n", inputosm_thread_count());

    const int thread_count = inputosm_thread_count();

    // cache line is 64B, not aligned as I'd wish but won't need more for this
    const size_t counter_stride_bytes = align_up(thread_count * sizeof(uint64_t), 64);
    printf("Pool size: %luB\n", 3 * counter_stride_bytes);
    uint64_t* pool = (uint64_t*)malloc(3 * counter_stride_bytes);
    const size_t counter_stride_elements = counter_stride_bytes / sizeof(pool[0]);
    for (size_t i = 0; i < 3 * counter_stride_elements; ++i)
    {
        pool[i] = 0;
    }

    Statistics s;
    s.nodes = (pool + 0 * counter_stride_elements);
    s.ways = (pool + 1 * counter_stride_elements);
    s.relations = (pool + 2 * counter_stride_elements);

    inputosm_callbacks_t callbacks;
    callbacks.user_data = &s;
    callbacks.node_handler = &nodes_callback;
    callbacks.way_handler = &ways_callback;
    callbacks.relation_handler = &relations_callback;

    inputosm_input_file(argv[1], argc > 2, callbacks);

    uint64_t nodes_total = 0, ways_total = 0, relations_total = 0;
    for (int i = 0; i < thread_count; ++i)
    {
        nodes_total += s.nodes[i];
        ways_total += s.ways[i];
        relations_total += s.relations[i];
    }

    printf("nodes: %lu\n", nodes_total);
    printf("ways: %lu\n", ways_total);
    printf("relations: %lu\n", relations_total);

    free(pool);
}