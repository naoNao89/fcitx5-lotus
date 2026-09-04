function(add_lotus_headless_test target source labels)
    add_executable(${target} ${source} ${fcitx_lotus_core_sources})
    target_include_directories(${target} PRIVATE
        ${PROJECT_BINARY_DIR}
        ${PROJECT_SOURCE_DIR}/src
        ${PROJECT_SOURCE_DIR}/data/emoji)
    target_compile_definitions(${target} PRIVATE
        FCITX5_LOTUS_SETTINGS_PATH="${CMAKE_INSTALL_FULL_BINDIR}/fcitx5-lotus-settings")
    target_link_libraries(${target} PRIVATE
        Fcitx5::Core Fcitx5::Config Bamboo::Core Pthread::Pthread X11::X11)
    add_test(NAME ${target} COMMAND ${target})
    set_tests_properties(${target} PROPERTIES LABELS "${labels}" TIMEOUT 20)
endfunction()

function(add_bamboo_test target source labels)
    add_executable(${target} ${source})
    target_link_libraries(${target} PRIVATE Bamboo::Core)
    add_test(NAME ${target} COMMAND ${target})
    set_tests_properties(${target} PROPERTIES LABELS "${labels}" TIMEOUT 20)
endfunction()

add_lotus_headless_test(smooth_buffered_key_replay
    smooth-buffered-key-replay.cpp "integration;smooth")
