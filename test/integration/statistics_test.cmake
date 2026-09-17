execute_process(COMMAND "${FIXTURE_GENERATOR}" "${FIXTURE}" RESULT_VARIABLE result)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "Cannot create the statistics fixture: ${result}")
endif()

foreach(metadata IN ITEMS OFF ON)
    set(arguments "${FIXTURE}")
    if(metadata)
        list(APPEND arguments 1)
    endif()
    execute_process(
        COMMAND "${PROGRAM}" ${arguments}
        RESULT_VARIABLE result
        OUTPUT_VARIABLE output
        ERROR_VARIABLE error
    )
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "Statistics failed: ${result}\n${error}")
    endif()
    foreach(expected IN ITEMS
        "nodes: 7\nways: 3\nrelations: 3\n"
        "max nodes per block: 5\nmax node tags per block: 3\n"
        "max ways per block: 2\nmax way tags per block: 2\nmax way nodes per block: 5\n"
        "max relations per block: 2\nmax relation tags per block: 3\nmax relation members per block: 4\n"
        "max file block index: 4\n"
        "nodes with tags: 3\nways with tags: 2\nrelations with tags: 2\n"
        "max node id: 100\nmax way id: 90\nmax relation id: 91\n"
    )
        string(FIND "${output}" "${expected}" position)
        if(position EQUAL -1)
            message(FATAL_ERROR "Missing statistics '${expected}':\n${output}")
        endif()
    endforeach()
    if(metadata)
        set(timestamps
            "node|2223-07-06 14:13:20"
            "way|2109-06-06 22:13:20"
            "relation|2118-12-09 03:33:20"
        )
    else()
        set(timestamps
            "node|1970-01-01 00:00:00"
            "way|1970-01-01 00:00:00"
            "relation|1970-01-01 00:00:00"
        )
    endif()
    foreach(timestamp IN LISTS timestamps)
        string(REPLACE "|" " timestamp: " expected "${timestamp}")
        string(FIND "${output}" "max ${expected} GMT\n" position)
        if(position EQUAL -1)
            message(FATAL_ERROR "Incorrect timestamp '${expected}':\n${output}")
        endif()
    endforeach()
endforeach()
file(REMOVE "${FIXTURE}")
