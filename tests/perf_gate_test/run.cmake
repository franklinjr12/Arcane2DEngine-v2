# Deterministic test of tests/perf_gate.cmake (docs/perf_gate.md). Run with cmake -P:
#   -DGATE=path/perf_gate.cmake -DFAKE_BENCH=path/fake_bench.cmake -DWORK_DIR=dir
cmake_minimum_required(VERSION 3.19)

file(REMOVE_RECURSE "${WORK_DIR}")
file(MAKE_DIRECTORY "${WORK_DIR}")
set(baseline "${WORK_DIR}/baseline.json")

function(fresh_baseline)
  file(WRITE "${baseline}" "{
  \"baselines\": {
    \"Test-X/testarch\": {
      \"arch\": \"testarch\",
      \"cases\": {
        \"particles\": {\"args\": \"\", \"avg_frame_ms\": 1.000000},
        \"physics\": {\"args\": \"\", \"avg_frame_ms\": 1.000000},
        \"scene\": {\"args\": \"\", \"avg_frame_ms\": 1.000000}
      },
      \"flavor\": \"Test-X\",
      \"runs\": 5,
      \"threshold_percent\": 20
    }
  },
  \"format\": \"arcane.perf_baseline\",
  \"version\": 2
}
")
endfunction()

# run_gate(ARCH PERCENT) sets rc and output in the parent scope.
function(run_gate arch percent)
  execute_process(
    COMMAND ${CMAKE_COMMAND} -E env ARCANE_PERF_GATE_SCALE_PERCENT=${percent}
            ${CMAKE_COMMAND} -DARCH=${arch} -DFLAVOR=Test-X -DBENCH=${FAKE_BENCH} -DBASELINE=${baseline} -P ${GATE}
    RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
  set(rc "${rc}" PARENT_SCOPE)
  set(output "${out}\n${err}" PARENT_SCOPE)
endfunction()

fresh_baseline()
run_gate(testarch 100)
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "100% must pass, rc=${rc}\n${output}")
endif()
run_gate(testarch 119)
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "+19% must pass, rc=${rc}\n${output}")
endif()
foreach(pct 121 125)
  run_gate(testarch ${pct})
  if(rc EQUAL 0)
    message(FATAL_ERROR "+${pct}% over a 20% threshold must fail\n${output}")
  endif()
  if(NOT output MATCHES "FAIL")
    message(FATAL_ERROR "+${pct}% failure output lacks FAIL\n${output}")
  endif()
endforeach()

# A missing architecture is recorded with a warning and passes; the existing entry is untouched.
fresh_baseline()
file(READ "${baseline}" before)
run_gate(otherarch 100)
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "missing arch must pass, rc=${rc}\n${output}")
endif()
if(NOT output MATCHES "no baseline for Test-X/otherarch")
  message(FATAL_ERROR "missing arch must warn\n${output}")
endif()
file(READ "${baseline}" after)
string(JSON old_entry GET "${before}" baselines "Test-X/testarch")
string(JSON kept GET "${after}" baselines "Test-X/testarch")
string(JSON recorded ERROR_VARIABLE rec_err GET "${after}" baselines "Test-X/otherarch")
if(NOT old_entry STREQUAL kept OR rec_err)
  message(FATAL_ERROR "baseline merge wrong:\n${after}")
endif()
message(STATUS "perf_gate_threshold ok")
