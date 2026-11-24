if (NOT INPUT_FILE OR NOT OUTPUT_FILE OR NOT VARIABLE_NAME)
    message(FATAL_ERROR "INPUT_FILE, OUTPUT_FILE and VARIABLE_NAME must be defined")
endif()

file(READ ${INPUT_FILE} SHADER_CONTENT)

# One string literal per line, which the compiler joins: MSVC refuses a single literal of more than about
# 16 KB (C2026), and generic.frag has grown to that size.
string(REPLACE "\n" "\\n\"\n\"" SHADER_CONTENT "${SHADER_CONTENT}")
string(REPLACE "\r" "\\r" SHADER_CONTENT "${SHADER_CONTENT}")
string(REPLACE "\t" "\\t" SHADER_CONTENT "${SHADER_CONTENT}")

file(WRITE ${OUTPUT_FILE} "const char *${VARIABLE_NAME} = \"${SHADER_CONTENT}\";")