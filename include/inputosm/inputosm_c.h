#ifndef INPUTOSM_C_H
#define INPUTOSM_C_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C"
{
#endif

    typedef enum
    {
        INPUTOSM_PBF,
        INPUTOSM_XML,
        INPUTOSM_FILETYPE_LAST
    } inputosm_file_type_t;

    typedef enum
    {
        INPUTOSM_BULK,
        INPUTOSM_CREATE,
        INPUTOSM_MODIFY,
        INPUTOSM_DESTROY,
        INPUTOSM_MODE_LAST
    } inputosm_mode_t;

    /**
     * @brief Represents a tag as per OSM
     */
    typedef struct tag
    {
        const char* key;
        const char* value;
    } inputosm_tag_t;

    /**
     * @brief Represents a node as per OSM
     */
    typedef struct
    {
        int64_t id;
        int64_t raw_latitude;
        int64_t raw_longitude;
        inputosm_tag_t* tags;
        uint64_t tags_size;
        int32_t version;
        int32_t timestamp;
        int32_t changeset;
    } inputosm_node_t;

    /**
     * @brief Represents a way as per OSM
     */
    typedef struct
    {
        int64_t id;
        int64_t* node_refs;
        uint64_t node_refs_size;
        inputosm_tag_t* tags;
        uint64_t tags_size;
        int32_t version;
        int32_t timestamp;
        int32_t changeset;
    } inputosm_way_t;

    /**
     * @brief Represents a relationship member as per OSM
     */
    typedef struct
    {
        uint8_t type;
        int64_t id;
        const char* role;
    } inputosm_relation_member_t;

    /**
     * @brief Represents a relationship as per OSM
     */
    typedef struct
    {
        int64_t id;
        inputosm_relation_member_t* members;
        uint64_t members_size;
        inputosm_tag_t* tags;
        uint64_t tags_size;
        int32_t version;
        int32_t timestamp;
        int32_t changeset;
    } inputosm_relation_t;

    /**
     * @brief Represents the available logging levels
     */
    typedef enum
    {
        INPUTOSM_LOG_LEVEL_TRACE,
        INPUTOSM_LOG_LEVEL_INFO = 4,
        INPUTOSM_LOG_LEVEL_ERROR = 7,
        INPUTOSM_LOG_LEVEL_DISABLED = 255,
    } inputosm_log_level_t;

    /**
     * Set thread count to use
     * @param size is the number of threads you want
     */
    void inputosm_set_thread_count(size_t);

    /**
     * @brief Set to maximum available
     */
    void inputosm_set_max_thread_count();

    /**
     * @brief Get the actual number of threads used
     */
    size_t inputosm_thread_count();

    /**
     * @brief Get the working thread index
     */
    size_t inputosm_thread_index();

    typedef bool (*inputosm_node_callback_t)(void*, const inputosm_node_t*, size_t);
    typedef bool (*inputosm_way_callback_t)(void*, const inputosm_way_t*, size_t);
    typedef bool (*inputosm_relation_callback_t)(void*, const inputosm_relation_t*, size_t);

    /**
     * @brief Handlers for the various entities
     */
    typedef struct
    {
        void* user_data;
        inputosm_node_callback_t node_handler;
        inputosm_way_callback_t way_handler;
        inputosm_relation_callback_t relation_handler;
    } inputosm_callbacks_t;

    /**
     * @brief load and process a file, calling appropriate callbacks based on OSM entities
     * @param filename a zero terminated string containing the name of the file to open
     * @param decode_metadata should metadata be decoded?
     * @param handlers user provided callbacks and user data for each entity type
     */
    bool inputosm_input_file(const char* filename, bool decode_metadata, inputosm_callbacks_t handlers);

    /**
     * @brief Set verbose output
     */
    void inputosm_set_verbose(bool value);

    /**
     * @brief Set log level
     * @note not thread safe
     */
    void inputosm_set_log_level(inputosm_log_level_t level);

    /**
     * @brief Log callback used for reporting back to the user
     * @note the message is a \0 terminated c-string
     */
    typedef void (*inputosm_log_callback_t)(inputosm_log_level_t, const char*);

    /**
     * @brief Set the log callback
     * @param log_callback new log callback
     * @note the log callback will be called from multiple threads, it should be thread safe
     * @return true if set was OK
     */
    bool inputosm_set_log_callback(inputosm_log_callback_t log_callback);

#ifdef __cplusplus
}
#endif

#endif // INPUTOSM_C_H
