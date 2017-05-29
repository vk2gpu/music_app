INCLUDE_DIRECTORIES(
	"${APP_3RDPARTY_PATH}/portaudio"
	"${APP_3RDPARTY_PATH}/portmidi/pm_common"
	"${ENGINE_3RDPARTY_PATH}/stb"
)


SET(SOURCES
	"app.h"
	"app.cpp"
	"main.cpp"
	"settings.h"
	"settings.cpp"
)

SET(SOURCES_BACKEND
	"audio_backend.h"
	"audio_backend.cpp"
	"midi_backend.h"
	"midi_backend.cpp"
)

SET(SOURCES_CALLBACKS
	"audio_buffer_callback.h"
	"audio_buffer_callback.cpp"
	"audio_recording_callback.h"
	"audio_recording_callback.cpp"
	"audio_stats_callback.h"
	"audio_stats_callback.cpp"
)

SET(SOURCES_GUI
	"gui.h"
	"gui.cpp"
	"dialog_device_selection.h"
	"dialog_device_selection.cpp"
)

SET(SOURCES_UTILITY
	"midi.h"
	"midi.cpp"
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
ADD_ENGINE_EXECUTABLE(music_app ${SOURCES} ${SOURCES_CALLBACKS} ${SOURCES_BACKEND} ${SOURCES_GUI} ${SOURCES_UTILITY} ${SOURCES_ISPC})
TARGET_LINK_LIBRARIES(music_app graphics imgui serialization portaudio_static portmidi-static)

SOURCE_GROUP("Source" FILES ${SOURCES})
SOURCE_GROUP("Source\\Backend" FILES ${SOURCES_BACKEND})
SOURCE_GROUP("Source\\Callback" FILES ${SOURCES_CALLBACKS})
SOURCE_GROUP("Source\\GUI" FILES ${SOURCES_GUI})
SOURCE_GROUP("Source\\Utility" FILES ${SOURCES_UTILITY})
