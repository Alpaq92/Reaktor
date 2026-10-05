execute_process(COMMAND "${AR}" -M INPUT_FILE "${MRI}" RESULT_VARIABLE _rc)
if(_rc)
    message(FATAL_ERROR "merging the archives failed: ${_rc}")
endif()
