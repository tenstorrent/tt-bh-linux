
# Add source files
set(SOURCES
    blackhole_pcie.cpp
    utility.cpp
)

# Create the library
add_library(blackhole_thing ${SOURCES})
target_link_libraries(blackhole_thing fmt Threads::Threads)
