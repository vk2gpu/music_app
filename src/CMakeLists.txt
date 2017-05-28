INCLUDE_DIRECTORIES(
	"${APP_3RDPARTY_PATH}/portaudio"
	"${APP_3RDPARTY_PATH}/portmidi/pm_common"
	"${ENGINE_3RDPARTY_PATH}/stb"
)


SET(SOURCES
	"audio_backend.h"
	"audio_backend.cpp"
	"dialog_device_selection.h"
	"dialog_device_selection.cpp"
	"main.cpp"
	"midi_backend.h"
	"midi_backend.cpp"
	"note.h"
	"note.cpp"
	"sound.h"
	"sound.cpp"
)

SET(SOURCES_ISPC
	"ispc/acf.ispc"
	"ispc/audio_stats.ispc"
	"ispc/clipping.ispc"
	"ispc/biquad_filter.ispc"
)

# Add music_app.
ADD_ENGINE_EXECUTABLE(music_app ${SOURCES} ${SOURCES_ISPC})
TARGET_LINK_LIBRARIES(music_app graphics imgui serialization portaudio_static portmidi-static)

