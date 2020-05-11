if (NOT DEFINED STATIC_LIB_NAMES)
  message(FATAL_ERROR "STATIC_LIB_NAMES must be set")
endif()

set(SSL_PROJECT_NAME ssl)

list(APPEND STATIC_LIB_NAMES ${SSL_PROJECT_NAME})

set(SSL_SOURCE_FILES #[[add source files here]])

add_library(${SSL_PROJECT_NAME} ${SSL_SOURCE_FILES})