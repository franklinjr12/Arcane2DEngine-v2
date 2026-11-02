# Headless bench regression gate (IN6, docs/perf_gate.md). Run with cmake -P:
#   -DBENCH=path/to/arcane_bench -DBASELINE=docs/perf_baseline.json -DFLAVOR=Debug-GNU
#   [-DMODE=check|update] [-DFORCE=ON]
# check:  runs each case RUNS times, takes the median avg_frame_ms and fails if it is more than
#         the baseline's threshold_percent above the baseline. A baseline recorded for another
#         FLAVOR (build type + compiler + sanitizer) is skipped (exit code 77) unless FORCE=ON.
# update: rewrites BASELINE from fresh medians for this FLAVOR.
# ARCANE_PERF_GATE_SCALE=N (env) multiplies every measurement, to prove that a slow path fails.
# Times are handled as integer nanoseconds because cmake has no floating point.
cmake_minimum_required(VERSION 3.19)

if(NOT MODE)
  set(MODE check)
endif()
set(RUNS 5)
set(CASES scene particles physics)
set(ARGS_scene --sprites 1000 --emitters 0 --bodies 0)
set(ARGS_particles --sprites 0 --emitters 10 --particles-per-emitter 100 --bodies 0)
set(ARGS_physics --sprites 0 --emitters 0 --bodies 200)

# "0.31090" -> 310900. Exponent forms (below a microsecond) count as 1 ns.
function(ms_to_ns text out)
  if(text MATCHES "^([0-9]+)(\\.([0-9]+))?$")
    set(frac "${CMAKE_MATCH_3}000000")
    string(SUBSTRING "${frac}" 0 6 frac)
    math(EXPR ns "${CMAKE_MATCH_1} * 1000000 + ${frac}" OUTPUT_FORMAT DECIMAL)
    string(REGEX REPLACE "^0+([0-9])" "\\1" ns "${ns}")
  else()
    set(ns 1)
  endif()
  set(${out} "${ns}" PARENT_SCOPE)
endfunction()

# 310900 -> "0.310900"
function(ns_to_ms ns out)
  string(LENGTH "${ns}" len)
  if(len LESS 7)
    set(pad "0000000${ns}")
    string(LENGTH "${pad}" plen)
    math(EXPR start "${plen} - 6")
    string(SUBSTRING "${pad}" ${start} 6 frac)
    set(${out} "0.${frac}" PARENT_SCOPE)
  else()
    math(EXPR ipart_len "${len} - 6")
    string(SUBSTRING "${ns}" 0 ${ipart_len} ipart)
    string(SUBSTRING "${ns}" ${ipart_len} 6 frac)
    set(${out} "${ipart}.${frac}" PARENT_SCOPE)
  endif()
endfunction()

function(median_ns case out)
  set(values "")
  foreach(i RANGE 1 ${RUNS})
    execute_process(COMMAND "${BENCH}" ${ARGS_${case}} RESULT_VARIABLE rc OUTPUT_VARIABLE json ERROR_VARIABLE err)
    if(NOT rc EQUAL 0)
      message(FATAL_ERROR "arcane_bench failed for case ${case}: ${err}")
    endif()
    string(JSON ms GET "${json}" results avg_frame_ms)
    ms_to_ns("${ms}" ns)
    if(DEFINED ENV{ARCANE_PERF_GATE_SCALE})
      math(EXPR ns "${ns} * $ENV{ARCANE_PERF_GATE_SCALE}")
    endif()
    list(APPEND values ${ns})
  endforeach()
  list(SORT values COMPARE NATURAL)
  math(EXPR mid "${RUNS} / 2")
  list(GET values ${mid} median)
  set(${out} "${median}" PARENT_SCOPE)
endfunction()

if(MODE STREQUAL "update")
  set(threshold 30)
  set(body "")
  set(sep "")
  foreach(case ${CASES})
    median_ns(${case} ns)
    ns_to_ms(${ns} ms)
    string(REPLACE ";" " " argtext "${ARGS_${case}}")
    string(APPEND body "${sep}    \"${case}\": {\"args\": \"${argtext}\", \"avg_frame_ms\": ${ms}}")
    set(sep ",\n")
    message(STATUS "${case}: ${ms} ms")
  endforeach()
  file(WRITE "${BASELINE}"
"{
  \"cases\": {
${body}
  },
  \"flavor\": \"${FLAVOR}\",
  \"format\": \"arcane.perf_baseline\",
  \"runs\": ${RUNS},
  \"threshold_percent\": ${threshold},
  \"version\": 1
}
")
  message(STATUS "wrote ${BASELINE}")
  return()
endif()

file(READ "${BASELINE}" base)
string(JSON base_flavor GET "${base}" flavor)
string(JSON threshold GET "${base}" threshold_percent)
if(NOT base_flavor STREQUAL FLAVOR AND NOT FORCE)
  message(STATUS "perf gate skipped: baseline is for '${base_flavor}', this build is '${FLAVOR}'")
  message(STATUS "refresh with the perf_baseline_update target (docs/perf_gate.md)")
  cmake_language(EXIT 77)
endif()

set(failed "")
foreach(case ${CASES})
  string(JSON base_ms GET "${base}" cases ${case} avg_frame_ms)
  ms_to_ns("${base_ms}" base_ns)
  median_ns(${case} ns)
  math(EXPR limit "${base_ns} * (100 + ${threshold}) / 100")
  math(EXPR pct "(${ns} - ${base_ns}) * 100 / ${base_ns}")
  ns_to_ms(${ns} ms)
  if(ns GREATER limit)
    message(STATUS "FAIL ${case}: median ${ms} ms vs baseline ${base_ms} ms (${pct}%, limit +${threshold}%)")
    list(APPEND failed ${case})
  else()
    message(STATUS "ok   ${case}: median ${ms} ms vs baseline ${base_ms} ms (${pct}%)")
  endif()
endforeach()
if(failed)
  message(FATAL_ERROR "perf regression in: ${failed}")
endif()
