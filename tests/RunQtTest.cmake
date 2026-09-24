# Runs one Qt Test executable (TEST_EXE) with its log in TEST_LOG, prints the
# log, and passes only if Qt Test reached its own summary with nothing failed.
# The exit code alone is not enough: some plugins end the process with
# exit(0) on a fatal error, which would otherwise look like a pass.
execute_process(COMMAND "${TEST_EXE}" -o "${TEST_LOG},txt" RESULT_VARIABLE code)
set(log "")
if(EXISTS "${TEST_LOG}")
    file(READ "${TEST_LOG}" log)
    message("${log}")
endif()
if(NOT log MATCHES "Totals: [0-9]+ passed, 0 failed")
    message(FATAL_ERROR "${TEST_EXE} stopped before its summary or had failures (exit code ${code}). "
                        "If the log ends mid-test, something ended the process (for example a plugin's fatal error).")
endif()
if(NOT code EQUAL 0)
    message(FATAL_ERROR "${TEST_EXE} exited with code ${code}")
endif()
