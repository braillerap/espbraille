file(GLOB_RECURSE SOURCES *.c)


idf_component_register(SRCS "${SOURCES}"
                    INCLUDE_DIRS "include" "react"
                    EMBED_FILES root.html)
