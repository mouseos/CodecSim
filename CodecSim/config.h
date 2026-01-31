#define PLUG_NAME "CodecSim"
#define PLUG_MFR "MouseSoft"
#define PLUG_VERSION_HEX 0x00010000
#define PLUG_VERSION_STR "1.0.0"
#define PLUG_UNIQUE_ID 'CdSm'
#define PLUG_MFR_ID 'Mous'
#define PLUG_URL_STR "https://mousesoft.booth.pm"
#define PLUG_EMAIL_STR "support@mousesoft.example.com"
#define PLUG_COPYRIGHT_STR "Copyright 2025 MouseSoft"
#define PLUG_CLASS_NAME CodecSim

#define BUNDLE_NAME "CodecSim"
#define BUNDLE_MFR "MouseSoft"
#define BUNDLE_DOMAIN "pm.booth.mousesoft"

#define SHARED_RESOURCES_SUBPATH "CodecSim"

#define PLUG_CHANNEL_IO "2-2"

// Latency will be set dynamically based on codec (100-200ms)
#define PLUG_LATENCY 0
#define PLUG_TYPE 0
#define PLUG_DOES_MIDI_IN 0
#define PLUG_DOES_MIDI_OUT 0
#define PLUG_DOES_MPE 0
#define PLUG_DOES_STATE_CHUNKS 1
#define PLUG_HAS_UI 1
#define PLUG_WIDTH 600
#define PLUG_HEIGHT 400
#define PLUG_FPS 60
#define PLUG_SHARED_RESOURCES 0
#define PLUG_HOST_RESIZE 0
#define PLUG_MIN_WIDTH 600
#define PLUG_MIN_HEIGHT 400
#define PLUG_MAX_WIDTH 600
#define PLUG_MAX_HEIGHT 400

#define AUV2_ENTRY CodecSim_Entry
#define AUV2_ENTRY_STR "CodecSim_Entry"
#define AUV2_FACTORY CodecSim_Factory
#define AUV2_VIEW_CLASS CodecSim_View
#define AUV2_VIEW_CLASS_STR "CodecSim_View"

#define AAX_TYPE_IDS 'CdS1'
#define AAX_TYPE_IDS_AUDIOSUITE 'CdSA'
#define AAX_PLUG_MFR_STR "MouseSoft"
#define AAX_PLUG_NAME_STR "CodecSim\nCodec Simulator"
#define AAX_PLUG_CATEGORY_STR "Effect"
#define AAX_DOES_AUDIOSUITE 1

#define VST3_SUBCATEGORY "Fx|Tools"

#define CLAP_MANUAL_URL "https://mousesoft.booth.pm"
#define CLAP_SUPPORT_URL "https://mousesoft.booth.pm"
#define CLAP_DESCRIPTION "Real-time lossy codec simulator for audio quality preview"
#define CLAP_FEATURES "audio-effect", "utility"

#define APP_NUM_CHANNELS 2
#define APP_N_VECTOR_WAIT 0
#define APP_MULT 1
#define APP_COPY_AUV3 0
#define APP_SIGNAL_VECTOR_SIZE 64

#define ROBOTO_FN "Roboto-Regular.ttf"
