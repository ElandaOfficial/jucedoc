cmake_minimum_required(VERSION 3.16)

set(JUCEDOC_BOT_NAME    "JuceDoc")
set(JUCEDOC_BOT_VERSION "1.0.0")
set(JUCEDOC_BOT_AUTHOR  "Elanda")

set(JUCEDOC_BOT_TOKEN "")
set(JUCEDOC_BOT_ID    "")

set(CMAKE_CXX_STANDARD          17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

project(JuceDoc
    VERSION   ${JUCEDOC_BOT_VERSION}
    LANGUAGES CXX)

add_subdirectory(lib)

juce_add_console_app(JuceDoc
    VERSION      ${JUCEDOC_BOT_NAME}
    COMPANY_NAME ${JUCEDOC_BOT_VERSION}
    PRODUCT_NAME ${JUCEDOC_BOT_AUTHOR}
    NEEDS_CURL   ON)

target_include_directories(JuceDoc
    PRIVATE
        ${PROJECT_SOURCE_DIR}/lib/doxygen/src
        ${PROJECT_SOURCE_DIR}/libversion
        ${GENERATED_SRC}
        lib/zip_view)

target_compile_definitions(JuceDoc
    PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_BOT_INFO_NAME="${JUCEDOC_BOT_NAME}"
        JUCE_BOT_INFO_VERSION="${JUCEDOC_BOT_VERSION}"
        JUCE_BOT_INFO_VENDOR="${JUCEDOC_BOT_AUTHOR}"
        JUCEDOC_BOT_TOKEN="${JUCEDOC_BOT_TOKEN}"
        JUCEDOC_BOT_ID="${JUCEDOC_BOT_ID}")

add_subdirectory(src)

target_link_libraries(JuceDoc
    PRIVATE
        # Bot libs
        sleepy-discord
        juce::juce_core
        jaut::jaut_core
        spdlog
        venum

        # Doxygen
        doxymain
        md5
        xml
        lodepng
        mscgen
        doxygen_version
        doxycfg
        vhdlparser)
