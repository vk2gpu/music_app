INCLUDE_DIRECTORIES(
	"${APP_3RDPARTY_PATH}/portaudio"
)


SET(SOURCES
	"main.cpp"
	"note.h"
	"note.cpp"
)

SET(SOURCES_ISPC
	"ispc/acf.ispc"
)

# Add sandbox_app.
ADD_ISPC_EXECUTABLE(music_app ${SOURCES} ${SOURCES_ISPC})
TARGET_LINK_LIBRARIES(music_app graphics imgui portaudio_static)

SET_TARGET_PROPERTIES(music_app PROPERTIES FOLDER App)
SOURCE_GROUP("Source" FILES ${SOURCES})
SOURCE_GROUP("ISPC" FILES ${SOURCES_ISPC})

