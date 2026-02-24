cmake_minimum_required(VERSION 3.20)

project(kabot LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)

# macOS: 自动查找 Homebrew 安装的 OpenSSL
set(KABOT_USING_VCPKG OFF)
if(CMAKE_TOOLCHAIN_FILE MATCHES "vcpkg")
  set(KABOT_USING_VCPKG ON)
endif()

if(APPLE)
  execute_process(
    COMMAND brew --prefix openssl@3
    OUTPUT_VARIABLE OPENSSL_ROOT_DIR
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
  )
  if(NOT OPENSSL_ROOT_DIR)
    execute_process(
      COMMAND brew --prefix openssl
      OUTPUT_VARIABLE OPENSSL_ROOT_DIR
      OUTPUT_STRIP_TRAILING_WHITESPACE
      ERROR_QUIET
    )
  endif()
  if(OPENSSL_ROOT_DIR)
    message(STATUS "Found OpenSSL via Homebrew: ${OPENSSL_ROOT_DIR}")
  endif()

  if(NOT KABOT_USING_VCPKG)
    execute_process(
      COMMAND brew --prefix boost
      OUTPUT_VARIABLE BOOST_ROOT
      OUTPUT_STRIP_TRAILING_WHITESPACE
      ERROR_QUIET
    )
    if(BOOST_ROOT)
      message(STATUS "Found Boost via Homebrew: ${BOOST_ROOT}")
    endif()
  endif()
endif()

option(KABOT_ENABLE_TELEGRAM "Enable Telegram channel support" ON)
option(KABOT_ENABLE_LARK "Enable Lark channel support" ON)


FetchContent_Declare(
  nlohmann_json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG v3.11.3
)

FetchContent_Declare(
  httplib
  GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
  GIT_TAG v0.15.3
)

FetchContent_Declare(
  croncpp
  GIT_REPOSITORY https://github.com/mariusbancila/croncpp.git
  GIT_TAG master
)

if(KABOT_ENABLE_LARK)
  FetchContent_Declare(
    lark_cpp
    GIT_REPOSITORY https://github.com/EastTower16/lark_cpp.git
    GIT_TAG main
  )
endif()

FetchContent_MakeAvailable(nlohmann_json httplib croncpp)
if(KABOT_ENABLE_LARK)
  FetchContent_MakeAvailable(lark_cpp)
endif()

find_package(OpenSSL REQUIRED)
find_package(Threads REQUIRED)
find_package(CURL REQUIRED)
if(BOOST_ROOT AND NOT KABOT_USING_VCPKG)
  set(BOOST_ROOT ${BOOST_ROOT})
  set(BOOST_INCLUDEDIR ${BOOST_ROOT}/include)
  set(BOOST_LIBRARYDIR ${BOOST_ROOT}/lib)
endif()
find_package(Boost REQUIRED COMPONENTS filesystem system process)
message(STATUS "Found CURL: ${CURL_VERSION_STRING} (${CURL_LIBRARIES})")

if(KABOT_ENABLE_TELEGRAM)
  FetchContent_Declare(
    tgbotcpp
    GIT_REPOSITORY https://github.com/reo7sp/tgbot-cpp.git
    GIT_TAG v1.7
  )
  FetchContent_MakeAvailable(tgbotcpp)
  
  # Patch: fix Boost 1.70+ compatibility (io_service -> io_context)
  if(tgbotcpp_SOURCE_DIR)
    file(READ "${tgbotcpp_SOURCE_DIR}/include/tgbot/net/BoostHttpOnlySslClient.h" BOOSTHTTPONLYSSLCLIENT_H_CONTENT)
    string(REPLACE "boost::asio::io_service" "boost::asio::io_context" BOOSTHTTPONLYSSLCLIENT_H_CONTENT "${BOOSTHTTPONLYSSLCLIENT_H_CONTENT}")
    file(WRITE "${tgbotcpp_SOURCE_DIR}/include/tgbot/net/BoostHttpOnlySslClient.h" "${BOOSTHTTPONLYSSLCLIENT_H_CONTENT}")
    
    file(READ "${tgbotcpp_SOURCE_DIR}/include/tgbot/net/HttpServer.h" HTTPSERVER_H_CONTENT)
    string(REPLACE "boost::asio::io_service" "boost::asio::io_context" HTTPSERVER_H_CONTENT "${HTTPSERVER_H_CONTENT}")
    file(WRITE "${tgbotcpp_SOURCE_DIR}/include/tgbot/net/HttpServer.h" "${HTTPSERVER_H_CONTENT}")

    file(READ "${tgbotcpp_SOURCE_DIR}/src/net/BoostHttpOnlySslClient.cpp" BOOSTHTTPONLYSSLCLIENT_CPP_CONTENT)
    string(REPLACE "socket.set_verify_callback(ssl::rfc2818_verification(url.host));" "" BOOSTHTTPONLYSSLCLIENT_CPP_CONTENT "${BOOSTHTTPONLYSSLCLIENT_CPP_CONTENT}")
    string(REPLACE "_ioService.reset();" "_ioService.restart();" BOOSTHTTPONLYSSLCLIENT_CPP_CONTENT "${BOOSTHTTPONLYSSLCLIENT_CPP_CONTENT}")
    string(REPLACE "tcp::resolver::query query(url.host, \"443\");" "" BOOSTHTTPONLYSSLCLIENT_CPP_CONTENT "${BOOSTHTTPONLYSSLCLIENT_CPP_CONTENT}")
    string(REPLACE "connect(socket.lowest_layer(), resolver.resolve(query));" "connect(socket.lowest_layer(), resolver.resolve(url.host, \"443\"));" BOOSTHTTPONLYSSLCLIENT_CPP_CONTENT "${BOOSTHTTPONLYSSLCLIENT_CPP_CONTENT}")
    file(WRITE "${tgbotcpp_SOURCE_DIR}/src/net/BoostHttpOnlySslClient.cpp" "${BOOSTHTTPONLYSSLCLIENT_CPP_CONTENT}")
    message(STATUS "Patched tgbot-cpp for Boost compatibility")
  endif()
  
  if(TARGET TgBot)
    target_compile_definitions(TgBot PUBLIC
      BOOST_ASIO_ENABLE_DEPRECATED
      BOOST_ASIO_USE_TS_EXECUTOR_AS_DEFAULT)
    target_compile_definitions(TgBot PUBLIC HAVE_CURL)
    target_link_libraries(TgBot CURL::libcurl)
  endif()
endif()

add_library(kabot_core INTERFACE)

target_include_directories(kabot_core INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(kabot_core INTERFACE ${croncpp_SOURCE_DIR}/include)
target_link_libraries(kabot_core INTERFACE 
  httplib::httplib 
  nlohmann_json::nlohmann_json 
  OpenSSL::SSL 
  OpenSSL::Crypto
  CURL::libcurl
  Threads::Threads
  Boost::filesystem
  Boost::system
  Boost::process
)
if(TARGET Boost::headers)
  target_link_libraries(kabot_core INTERFACE Boost::headers)
endif()
target_include_directories(kabot_core INTERFACE ${Boost_INCLUDE_DIRS})
target_compile_definitions(kabot_core INTERFACE CPPHTTPLIB_OPENSSL_SUPPORT)

if(KABOT_ENABLE_TELEGRAM)
  target_link_libraries(kabot_core INTERFACE TgBot)
endif()
if(KABOT_ENABLE_LARK)
  target_link_libraries(kabot_core INTERFACE lark_cpp)
endif()

# 收集所有源文件
set(KABOT_SOURCES
  cli/commands.cpp
  config/config_loader.cpp
  providers/llm_provider.cpp
  providers/litellm_provider.cpp
  agent/agent_loop.cpp
  agent/context_builder.cpp
  agent/memory_store.cpp
  agent/skills_loader.cpp
  agent/tools/filesystem.cpp
  agent/tools/cron.cpp
  agent/tools/tts.cpp
  agent/tools/message.cpp
  agent/tools/shell.cpp
  agent/tools/spawn.cpp
  agent/tools/tool_registry.cpp
  agent/tools/web.cpp
  sandbox/sandbox_executor.cpp
  bus/message_bus.cpp
  channels/channel_base.cpp
  channels/channel_manager.cpp
  cron/cron_service.cpp
  heartbeat/heartbeat_service.cpp
  session/session_manager.cpp
)

if(KABOT_ENABLE_TELEGRAM)
  list(APPEND KABOT_SOURCES channels/telegram_channel.cpp)
endif()
if(KABOT_ENABLE_LARK)
  list(APPEND KABOT_SOURCES channels/lark_channel.cpp)
endif()

add_executable(kabot_cli ${KABOT_SOURCES})
target_link_libraries(kabot_cli PRIVATE kabot_core)

add_library(kabot_tts STATIC agent/tools/tts.cpp)
target_link_libraries(kabot_tts PRIVATE kabot_core)

add_executable(tts_test tts_test.cpp)
target_link_libraries(tts_test PRIVATE kabot_core kabot_tts)

if(APPLE)
  target_link_options(kabot_cli PRIVATE "-Wl,-no_warn_duplicate_libraries")
endif()

if(KABOT_ENABLE_TELEGRAM)
  target_link_libraries(kabot_cli PRIVATE TgBot)
endif()

# macOS: 设置 RPATH 以便找到动态库
if(APPLE)
  set_target_properties(kabot_cli PROPERTIES
    INSTALL_RPATH "@executable_path/../lib"
    BUILD_RPATH "${OPENSSL_ROOT_DIR}/lib"
  )
endif()
