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
endfunction()

check_output("${COUNT_ALL}" "nodes: 17,005\nways: 12\nrelations: 4\n")
check_output("${COUNT_BLOCKS}" "nodes=17,005 ways=12 relations=4")
check_output("${STATISTICS}" "nodes: 17,005" "max node id: 5000017004")
check_output("${LAT_STAT}" "17,005" "100.00%")
