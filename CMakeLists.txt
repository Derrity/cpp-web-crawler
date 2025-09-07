cmake_minimum_required(VERSION 3.16)
project(BookScraper CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Options
option(BUILD_TESTING "Build tests" OFF)

include(FetchContent)
find_package(Threads REQUIRED)

# Fetch cpr (HTTP client)
set(CPR_USE_SYSTEM_CURL OFF)
set(BUILD_CPR_TESTS OFF)
set(BUILD_CPR_SSL ON)
set(CPR_ENABLE_CURL_HTTP_ONLY OFF)

FetchContent_Declare(
  cpr
  GIT_REPOSITORY https://github.com/libcpr/cpr.git
  GIT_TAG 1.10.5
)

FetchContent_MakeAvailable(cpr)

# nlohmann/json for manifest
FetchContent_Declare(
  nlohmann_json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG v3.11.3
)
FetchContent_MakeAvailable(nlohmann_json)

add_executable(book_scraper
  src/main.cpp
  src/crawler.cpp
)

target_include_directories(book_scraper PRIVATE src)

target_link_libraries(book_scraper PRIVATE cpr::cpr nlohmann_json::nlohmann_json)
target_link_libraries(book_scraper PRIVATE Threads::Threads)

install(TARGETS book_scraper RUNTIME DESTINATION bin)
