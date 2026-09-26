if (NOT DEFINED MARKQL_FMT OR NOT DEFINED SOURCE_FILE OR NOT DEFINED WORK_FILE OR NOT DEFINED EXPECT_RESULT)
  message(FATAL_ERROR "MARKQL_FMT, SOURCE_FILE, WORK_FILE and EXPECT_RESULT are required")
endif()

get_filename_component(work_dir "${WORK_FILE}" DIRECTORY)
file(MAKE_DIRECTORY "${work_dir}")
execute_process(
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${SOURCE_FILE}" "${WORK_FILE}"
  RESULT_VARIABLE copy_result ERROR_VARIABLE copy_error
)
if (NOT copy_result EQUAL 0)
  message(FATAL_ERROR "failed to copy formatter fixture: ${copy_error}")
endif()

execute_process(
  COMMAND "${MARKQL_FMT}" --check "${WORK_FILE}"
  RESULT_VARIABLE fmt_result OUTPUT_VARIABLE fmt_output ERROR_VARIABLE fmt_error
)
if (NOT fmt_result EQUAL EXPECT_RESULT)
  message(FATAL_ERROR
    "markql-fmt returned ${fmt_result}, expected ${EXPECT_RESULT}\n"
    "stdout:\n${fmt_output}\n"
    "stderr:\n${fmt_error}"
  )
endif()
