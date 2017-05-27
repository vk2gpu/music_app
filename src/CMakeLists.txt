INCLUDE_DIRECTORIES(
	"${APP_3RDPARTY_PATH}/portaudio"
	"${ENGINE_3RDPARTY_PATH}/stb"
)


SET(SOURCES
	"audio_backend.h"
	"audio_backend.cpp"
	"main.cpp"
	"note.h"
	"note.cpp"
	"sound.h"
	"sound.cpp"
)

SET(SOURCES_ISPC
	"ispc/acf.ispc"
	"ispc/biquad_filter.ispc"
	"ispc/clipping.ispc"
)

# Add music_app.
ADD_ENGINE_EXECUTABLE(music_app ${SOURCES} ${SOURCES_ISPC})
TARGET_LINK_LIBRARIES(music_app graphics imgui serialization portaudio_static)

