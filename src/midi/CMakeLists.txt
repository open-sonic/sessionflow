add_library(sessionflow_midi
    midi_file.cpp
    midi_input_collector.cpp
    midi_inputs.cpp
)
target_link_libraries(sessionflow_midi
    PUBLIC sessionflow_core sessionflow_model
    PRIVATE libremidi sessionflow_warnings
)
