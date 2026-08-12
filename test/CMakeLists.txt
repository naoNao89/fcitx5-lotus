add_executable(regression_pr271_fake_bs_preedit
    regression-pr271-fake-bs-preedit.cpp
    ${fcitx_lotus_core_sources}
)

target_include_directories(regression_pr271_fake_bs_preedit PRIVATE
    ${PROJECT_BINARY_DIR}
    ${PROJECT_SOURCE_DIR}/src
)

target_compile_definitions(regression_pr271_fake_bs_preedit PRIVATE
    FCITX5_LOTUS_SETTINGS_PATH=\"${CMAKE_INSTALL_FULL_BINDIR}/fcitx5-lotus-settings\"
)

target_link_libraries(regression_pr271_fake_bs_preedit PRIVATE
    Fcitx5::Core
    Fcitx5::Config
    Fcitx5::Module::Emoji
    Bamboo::Core
    Pthread::Pthread
    X11::X11
)

add_test(NAME regression_pr271_fake_bs_preedit
    COMMAND regression_pr271_fake_bs_preedit)
set_tests_properties(regression_pr271_fake_bs_preedit PROPERTIES
    LABELS "integration;regression"
    TIMEOUT 20
)
