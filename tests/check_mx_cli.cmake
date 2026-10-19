execute_process(COMMAND "${PROGRAM}" compile "${ASSERT_SOURCE}"
  RESULT_VARIABLE compile_status OUTPUT_VARIABLE compiled ERROR_VARIABLE compile_error)
string(REPLACE "\r\n" "\n" compiled "${compiled}")
if (NOT compile_status EQUAL 0 OR NOT compile_error STREQUAL "" OR
    NOT compiled STREQUAL "backend compact\nqubits 1\nassert one\n")
  message(FATAL_ERROR "compile must emit only XASM and must not run assertions: ${compile_status}, ${compiled}, ${compile_error}")
endif()

execute_process(COMMAND "${PROGRAM}" run-mx "${ASSERT_SOURCE}"
  RESULT_VARIABLE run_status OUTPUT_VARIABLE run_output ERROR_VARIABLE run_error)
if (NOT run_status EQUAL 1 OR NOT run_output MATCHES "ASSERT one FAIL")
  message(FATAL_ERROR "run-mx must propagate assertion failure: ${run_status}, ${run_output}, ${run_error}")
endif()

execute_process(COMMAND "${PROGRAM}" compile "${INVALID_SOURCE}"
  RESULT_VARIABLE invalid_status OUTPUT_VARIABLE invalid_output ERROR_VARIABLE invalid_error)
if (NOT invalid_status EQUAL 1 OR NOT invalid_output STREQUAL "" OR
    NOT invalid_error MATCHES ":2:[0-9]+:")
  message(FATAL_ERROR "compile must reject semantic errors without partial stdout: ${invalid_status}, ${invalid_output}, ${invalid_error}")
endif()
