add_library(sessionflow_library
    database.cpp
    library_index.cpp
)
target_link_libraries(sessionflow_library
    PUBLIC sessionflow_core
    PRIVATE sqlite3 sessionflow_warnings
)
