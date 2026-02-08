# Toolchain hints for Windows / Linux-arm64

# Windows (MSVC)
# - Use: cmake -G "Visual Studio 17 2022" -A x64 -S cpp -B build
# - Ensure vcpkg or system OpenSSL is available if tgbot-cpp requires it

# Linux-arm64 (aarch64)
# - Example:
#   cmake -S cpp -B build \
#     -DCMAKE_SYSTEM_NAME=Linux \
#     -DCMAKE_SYSTEM_PROCESSOR=aarch64 \
#     -DCMAKE_C_COMPILER=aarch64-linux-gnu-gcc \
#     -DCMAKE_CXX_COMPILER=aarch64-linux-gnu-g++

# Shared compile options
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
