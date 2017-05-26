INCLUDE_DIRECTORIES(
	"${APP_3RDPARTY_PATH}/portaudio"
)


SET(SOURCES
	"audio_backend.h"
	"audio_backend.cpp"
	"main.cpp"
	"note.h"
	"note.cpp"
)

SET(SOURCES_ISPC
	"ispc/acf.ispc"
)

# Add sandbox_app.
ADD_ENGINE_EXECUTABLE(music_app ${SOURCES} ${SOURCES_ISPC})
TARGET_LINK_LIBRARIES(music_app graphics imgui serialization portaudio_static)

