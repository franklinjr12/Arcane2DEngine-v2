# Fake arcane_bench for perf_gate_threshold: always reports 1 ms per frame on stdout.
execute_process(COMMAND ${CMAKE_COMMAND} -E echo "{\"results\": {\"avg_frame_ms\": 1.000000}}")
