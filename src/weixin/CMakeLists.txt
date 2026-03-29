# WeChat (Weixin) Channel Implementation
# Converted from TypeScript to C++17

# SILK decoder library
FetchContent_Declare(
  silk_decoder
  GIT_REPOSITORY https://github.com/kn007/silk-v3-decoder.git
  GIT_TAG master
)

# Don't use FetchContent_MakeAvailable as silk doesn't have CMakeLists.txt
FetchContent_GetProperties(silk_decoder)
if(NOT silk_decoder_POPULATED)
  FetchContent_Populate(silk_decoder)
endif()

set(SILK_SOURCE_DIR ${silk_decoder_SOURCE_DIR}/silk)

# Collect all SILK source files
file(GLOB SILK_SOURCES 
  ${SILK_SOURCE_DIR}/src/*.c
)

# Create SILK static library
add_library(silk STATIC ${SILK_SOURCES})
target_include_directories(silk PUBLIC 
  ${SILK_SOURCE_DIR}/interface
  ${SILK_SOURCE_DIR}/src
)

set(WEIXIN_SOURCES
  ${CMAKE_CURRENT_SOURCE_DIR}/weixin_channel.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/compat.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/api/api_client.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/api/config_cache.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/api/session_guard.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/auth/accounts.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/auth/login_qr.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/auth/pairing.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/cdn/aes_ecb.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/cdn/cdn_upload.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/cdn/cdn_url.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/cdn/file_upload.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/cdn/pic_decrypt.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/config/config_schema.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/media/media_downloader.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/media/mime_detector.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/media/silk/silk_decoder.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/media/silk/silk_sdk_wrapper.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/media/silk_transcoder.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/messaging/inbound_processor.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/messaging/message_pipeline.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/messaging/message_sender.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/messaging/media_sender.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/messaging/slash_commands.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/messaging/debug_mode.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/messaging/error_notifier.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/monitor/connection_manager.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/monitor/poll_monitor.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/storage/state_directory.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/storage/sync_buffer.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/storage/context_token_store.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/storage/file_lock.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/util/logger.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/util/random.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/util/redact.cpp
)

# Weixin channel library
add_library(weixin_channel STATIC ${WEIXIN_SOURCES})

target_include_directories(weixin_channel PUBLIC 
  ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(weixin_channel PUBLIC 
  kabot_core
  silk
  ${Boost_LIBRARIES}
)

# Set C++ standard
set_target_properties(weixin_channel PROPERTIES
  CXX_STANDARD 17
  CXX_STANDARD_REQUIRED ON
  CXX_EXTENSIONS OFF
)

if(MSVC)
  set_target_properties(weixin_channel PROPERTIES
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
  )
endif()

# Export for parent scope
set(WEIXIN_CHANNEL_SOURCES ${WEIXIN_SOURCES} PARENT_SCOPE)
