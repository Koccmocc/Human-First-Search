# Exercise the real executable, including its runtime DLLs and exit codes.
function(check_cli expected_code expected_stdout expected_stderr)
    execute_process(
        COMMAND "${CRAWLER}" ${ARGN}
        RESULT_VARIABLE code
        OUTPUT_VARIABLE out
        ERROR_VARIABLE err
        TIMEOUT 5
    )
    string(REPLACE "\r\n" "\n" out "${out}")
    string(REPLACE "\r\n" "\n" err "${err}")
    if(NOT "${code}" STREQUAL "${expected_code}")
        message(FATAL_ERROR "Arguments '${ARGN}': exit ${code}, expected ${expected_code}; stderr: ${err}")
    endif()
    if(NOT out MATCHES "${expected_stdout}" OR NOT err MATCHES "${expected_stderr}")
        message(FATAL_ERROR "Arguments '${ARGN}': unexpected output. stdout: ${out}; stderr: ${err}")
    endif()
endfunction()

check_cli(0 "Usage: hfs-crawler" "^$")
check_cli(0 "Usage: hfs-crawler" "^$" --help)
check_cli(0 "Usage: hfs-crawler" "^$" -h)
check_cli(0 "^hfs-crawler ${EXPECTED_VERSION}\n$" "^$" --version)
check_cli(2 "^$" "Unknown argument: --unknown" --unknown)
check_cli(2 "^$" "Expected a single option" --help --unknown)
