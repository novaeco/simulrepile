idf_component_register(
    SRCS "save_manager_tests_dummy.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES unity fatfs wear_levelling
    REQUIRES main
)
