add_library(sessionflow_midi
    midi_input_collector.cpp
    midi_inputs.cpp
)
target_link_libraries(sessionflow_midi
    PUBLIC sessionflow_core
    PRIVATE libremidi sessionflow_warnings
)
