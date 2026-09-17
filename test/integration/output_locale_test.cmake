function(check_output program expected)
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E env
            LC_ALL=inputosm_missing_locale LANG=inputosm_missing_locale
            "${program}" "${INPUT}"
        RESULT_VARIABLE result
        OUTPUT_VARIABLE output
        ERROR_VARIABLE error
    )
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "${program} failed with an invalid locale: ${result}\n${error}")
    endif()
    foreach(value IN ITEMS "${expected}" ${ARGN})
        string(FIND "${output}" "${value}" position)
        if(position EQUAL -1)
            message(FATAL_ERROR "${program} output does not contain '${value}':\n${output}")
        endif()
    endforeach()
    set(LAST_OUTPUT "${output}" PARENT_SCOPE)
endfunction()

check_output("${COUNT_ALL}" "nodes: 17,005\nways: 12\nrelations: 4\n")
check_output("${COUNT_BLOCKS}" "nodes: 17,005\nways: 12\nrelations: 4\n")
check_output("${STATISTICS}" "nodes: 17,005" "max node id: 5000017004")
check_output("${LAT_STAT}" "17,005" "100.00%")

foreach(bucket IN ITEMS "44 +[|] +17,003" "45 +[|] +2")
    if(NOT LAST_OUTPUT MATCHES "[|] +${bucket} +[|]")
        message(FATAL_ERROR "Incorrect latitude bucket: ${bucket}\n${LAST_OUTPUT}")
    endif()
endforeach()
check_output("${EXTRACT_FERRIES}" "3 ferries" "17,005 unique nodes used by ferries")
