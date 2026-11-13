# =============================================================================
# Fuzz targets (docs/testing.md, "Fuzz tests")
# =============================================================================
# Each tests/fuzz/fuzz_<name>.cpp defines LLVMFuzzerTestOneInput() and is built twice:
#
#   fuzz_replay_<name>   any compiler; a CTest that replays tests/fuzz/corpus/fuzz_<name>/
#   fuzz_<name>          Clang with -DPCAL9555_BUILD_FUZZERS=ON: libFuzzer with ASan and UBSan
#
#   cmake -S . -B build-fuzz -DCMAKE_CXX_COMPILER=clang++ -DPCAL9555_BUILD_TESTS=ON -DPCAL9555_BUILD_FUZZERS=ON
#   cmake --build build-fuzz --target fuzz_pcal9555
#   ./build-fuzz/tests/fuzz/fuzz_pcal9555 -max_total_time=60 tests/fuzz/corpus/fuzz_pcal9555
# =============================================================================

file(GLOB PCAL9555_FUZZ_TARGETS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/fuzz_*.cpp)
foreach(src ${PCAL9555_FUZZ_TARGETS})
  get_filename_component(name ${src} NAME_WE)

  add_executable(fuzz_replay_${name} ${src} replay_main.cpp)
  pcal9555_test_target(fuzz_replay_${name})
  target_include_directories(fuzz_replay_${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
  add_test(NAME pcal9555_${name}_corpus
    COMMAND fuzz_replay_${name} ${CMAKE_CURRENT_SOURCE_DIR}/corpus/${name})

  if(name STREQUAL "fuzz_pcal9555")
    # (Not named fuzz_*: the fuzz job runs every fuzz_* executable it finds as a libFuzzer target.)
    # The same target with one edit (the port stops counting transfers). Its corpus run must abort,
    # which proves the promise "NoDevice follows at least one transfer" is checked under every
    # compiler, whatever order it evaluates the arguments of a call in.
    add_executable(mutant_replay_${name} ${src} replay_main.cpp)
    pcal9555_test_target(mutant_replay_${name})
    target_include_directories(mutant_replay_${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
    target_compile_definitions(mutant_replay_${name} PRIVATE PCAL9555_FUZZ_MUTANT_UNCOUNTED_TRANSFERS)
    add_test(NAME pcal9555_${name}_mutant_uncounted_transfers
      COMMAND ${CMAKE_COMMAND} -DEXE=$<TARGET_FILE:mutant_replay_${name}>
        -DCORPUS=${CMAKE_CURRENT_SOURCE_DIR}/corpus/${name}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/expect_broken_promise.cmake)
  endif()

  if(PCAL9555_BUILD_FUZZERS)
    add_executable(${name} ${src})
    pcal9555_test_target(${name})
    target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
    target_compile_options(${name} PRIVATE -fsanitize=fuzzer,address,undefined -fno-sanitize-recover=all -g -O1)
    target_link_options(${name} PRIVATE -fsanitize=fuzzer,address,undefined)
  endif()
endforeach()
