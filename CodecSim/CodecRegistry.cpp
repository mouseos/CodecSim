//==============================================================================
// CodecRegistry.cpp
// Dynamic codec detection and registry implementation
// Copyright 2025 MouseSoft
//==============================================================================
#include "CodecRegistry.h"
#include <algorithm>
#include <cstdio>
#ifdef _WIN32
#include <windows.h>
#include <debugapi.h>
static void DebugLogRegistry(const std::string& msg)
{
  FILE* f = fopen("D:\\ffmpeg_codec_debug.log", "a");
  if (f) {
    fprintf(f, "[CodecRegistry] %s\n", msg.c_str());
    fflush(f);
    fclose(f);
  }
  OutputDebugStringA(("[CodecRegistry] " + msg + "\n").c_str());
}
#else
static void DebugLogRegistry(const std::string& msg) { (void)msg; }
#endif
//==============================================================================
// Singleton
//==============================================================================
CodecRegistry& CodecRegistry::Instance()
{
  static CodecRegistry instance;
  return instance;
}
CodecRegistry::CodecRegistry()
{
  RegisterBuiltinCodecs();
}
//==============================================================================
// Built-in codec definitions
//==============================================================================
void CodecRegistry::RegisterBuiltinCodecs()
{
  mCodecs = {
    // MP3
    {
      "mp3",              // id
      "MP3",              // displayName
      "libmp3lame",       // encoderName
      "mp3",              // muxerFormat
      "mp3",              // demuxerFormat
      128,                // defaultBitrate
      8,                  // minBitrate
      320,                // maxBitrate
      1152,               // frameSize
      576,                // latencySamples
      "",                 // additionalArgs
      false,              // isLossless
      false,              // available (detected later)
      // options
      {
        {"mp3_channel", "Channel Mode", "-joint_stereo", CodecOptionType::Choice, 0, 0, 0,
          {{"Joint Stereo", "1"}, {"Stereo", "0"}}},
        {"mp3_abr", "ABR Mode", "-abr", CodecOptionType::Toggle, 0, 0, 1, {}},
        {"mp3_vbr", "VBR Quality", "-q:a", CodecOptionType::Choice, 0, 0, 0,
          {{"Off (CBR)", ""}, {"Extreme (~245k)", "0"}, {"Standard (~190k)", "2"}, {"Medium (~165k)", "4"}, {"Low (~115k)", "6"}, {"Minimum (~65k)", "9"}}},
      }
    },
    // AAC (LC)
    {
      "aac",
      "AAC",
      "aac",
      "adts",
      "aac",
      128,
      32,
      512,
      1024,
      2048,
      "",
      false,
      false,
      {
        {"aac_coder", "Coder", "-aac_coder", CodecOptionType::Choice, 0, 0, 0,
          {{"Two-loop", "twoloop"}, {"Fast", "fast"}}},
      }
    },
    // HE-AAC (libfdk_aac)
    {
      "heaac",
      "HE-AAC",
      "libfdk_aac",
      "adts",
      "aac",
      64,
      24,
      128,
      1024,
      2048,
      "-profile:a aac_he -afterburner 1",
      false,
      false,
      {
        {"heaac_vbr", "VBR Mode", "-vbr", CodecOptionType::Choice, 0, 0, 0,
          {{"CBR", "0"}, {"VBR 1", "1"}, {"VBR 2", "2"}, {"VBR 3", "3"}, {"VBR 4", "4"}, {"VBR 5", "5"}}},
      }
    },
    // Opus
    {
      "opus",
      "Opus",
      "libopus",
      "ogg",
      "ogg",
      128,
      6,
      510,
      960,
      312,
      "",
      false,
      false,
      {
        {"opus_app", "Application", "-application", CodecOptionType::Choice, 1, 0, 0,
          {{"VoIP", "voip"}, {"Audio", "audio"}, {"Low Delay", "lowdelay"}}},
        {"opus_vbr", "VBR Mode", "-vbr", CodecOptionType::Choice, 1, 0, 0,
          {{"Off", "off"}, {"On", "on"}, {"Constrained", "constrained"}}},
      }
    },
    // Vorbis
    {
      "vorbis",
      "Vorbis",
      "libvorbis",
      "ogg",
      "ogg",
      128,
      64,
      500,
      1024,
      512,
      "",
      false,
      false
    },
    // AC-3
    {
      "ac3",
      "AC-3",
      "ac3",
      "ac3",
      "ac3",
      192,
      32,
      640,
      1536,
      1536,
      "",
      false,
      false,
      {
        {"ac3_dialnorm", "Dialogue Norm", "-dialnorm", CodecOptionType::IntRange, -31, -31, -1, {}},
      }
    },
    // E-AC-3
    {
      "eac3",
      "E-AC-3",
      "eac3",
      "eac3",
      "eac3",
      192,
      32,
      6144,
      1536,
      1536,
      "",
      false,
      false
    },
    // FLAC (lossless)
    {
      "flac",
      "FLAC",
      "flac",
      "flac",
      "flac",
      0,
      0,
      0,
      4096,
      4096,
      "",
      true,
      false,
      {
        {"flac_compression", "Compression", "-compression_level", CodecOptionType::IntRange, 5, 0, 12, {}},
      }
    },
    // MP2
    {
      "mp2",
      "MP2",
      "libtwolame",
      "mp2",
      "mp3",
      192,
      64,
      384,
      1152,
      576,
      "",
      false,
      false,
      {
        {"mp2_mode", "Stereo Mode", "-mode", CodecOptionType::Choice, 0, 0, 0,
          {{"Auto", "auto"}, {"Stereo", "stereo"}, {"Joint", "joint_stereo"}, {"Mono", "mono"}}},
      }
    },
    // WMA v2
    {
      "wma",
      "WMA v2",
      "wmav2",
      "asf",
      "asf",
      128,
      32,
      192,
      2048,
      2048,
      "",
      false,
      false
    },
    // G.711 A-law
    {
      "alaw",
      "G.711 A-law",
      "pcm_alaw",
      "wav",
      "wav",
      64,
      64,
      64,
      160,
      160,
      "",
      false,
      false
    },
    // G.711 mu-law
    {
      "mulaw",
      "G.711 mu-law",
      "pcm_mulaw",
      "wav",
      "wav",
      64,
      64,
      64,
      160,
      160,
      "",
      false,
      false
    },
    // Speex (speech codec)
    {
      "speex",
      "Speex",
      "libspeex",
      "ogg",
      "ogg",
      24,
      2,
      44,
      320,
      320,
      "",
      false,
      false,
      {
        {"speex_quality", "CBR Quality", "-cbr_quality", CodecOptionType::IntRange, 8, 0, 10, {}},
        {"speex_vad", "VAD", "-vad", CodecOptionType::Toggle, 0, 0, 1, {}},
      }
    },
    // GSM 06.10 (8kHz mono only)
    {
      "gsm",
      "GSM 06.10",
      "libgsm",
      "gsm",
      "gsm",
      13,
      13,
      13,
      160,
      160,
      "-ar 8000 -ac 1",
      false,
      false
    },
    //==========================================================================
    // Tier 1: Bluetooth / Mobile / Surround
    //==========================================================================
    // AMR-NB (mobile phone call codec, 3GPP, 8kHz mono only)
    // Discrete modes: 4.75/5.15/5.90/6.70/7.40/7.95/10.20/12.20 kbps
    {
      "amrnb",
      "AMR-NB",
      "libopencore_amrnb",
      "amr",
      "amr",
      12,
      12,
      12,
      160,
      160,
      "-ar 8000 -ac 1 -b:a 12200",
      false,
      false
    },
    // AMR-WB (HD Voice / VoLTE, 3GPP, 16kHz mono only)
    // Discrete modes: 6.60/8.85/12.65/14.25/15.85/18.25/19.85/23.05/23.85 kbps
    {
      "amrwb",
      "AMR-WB",
      "libvo_amrwbenc",
      "amr",
      "amr",
      24,
      24,
      24,
      320,
      320,
      "-ar 16000 -ac 1 -b:a 23850",
      false,
      false
    },
    // aptX (Bluetooth, fixed 4:1 ratio)
    {
      "aptx",
      "aptX",
      "aptx",
      "aptx",
      "aptx",
      352,
      352,
      352,
      4,
      4,
      "",
      true,
      false
    },
    // aptX HD (Bluetooth HD, fixed ratio)
    {
      "aptxhd",
      "aptX HD",
      "aptx_hd",
      "aptx_hd",
      "aptx_hd",
      576,
      576,
      576,
      4,
      4,
      "",
      true,
      false
    },
    // SBC (Bluetooth A2DP mandatory codec)
    {
      "sbc",
      "SBC",
      "sbc",
      "sbc",
      "sbc",
      328,
      128,
      512,
      128,
      128,
      "",
      false,
      false
    },
    // DTS (surround sound)
    {
      "dts",
      "DTS",
      "dca",
      "dts",
      "dts",
      768,
      320,
      6144,
      512,
      512,
      "-strict experimental",
      false,
      false
    },
    //==========================================================================
    // Tier 2: Telephony / VoIP / ADPCM
    //==========================================================================
    // iLBC (WebRTC / VoIP, 8kHz mono only)
    {
      "ilbc",
      "iLBC",
      "libilbc",
      "ilbc",
      "ilbc",
      13,
      13,
      15,
      160,
      160,
      "-ar 8000 -ac 1",
      false,
      false,
      {
        {"ilbc_mode", "Frame Mode", "-mode", CodecOptionType::Choice, 1, 0, 0,
          {{"20ms", "20"}, {"30ms", "30"}}},
      }
    },
    // G.723.1 (ultra-low bitrate telephony, 8kHz mono, 6.3/5.3 kbps only)
    {
      "g7231",
      "G.723.1",
      "g723_1",
      "matroska",
      "matroska",
      6,
      5,
      6,
      240,
      240,
      "-ar 8000 -ac 1 -b:a 6300",
      false,
      false
    },
    // G.722 ADPCM (ISDN wideband telephony)
    {
      "g722",
      "G.722",
      "g722",
      "matroska",
      "matroska",
      64,
      64,
      64,
      320,
      320,
      "",
      true,
      false
    },
    // G.726 ADPCM (classic telephony, 8kHz mono only)
    {
      "g726",
      "G.726",
      "g726",
      "matroska",
      "matroska",
      32,
      16,
      40,
      160,
      160,
      "-ar 8000 -ac 1",
      false,
      false,
      {
        {"g726_code", "Code Size", "-code_size", CodecOptionType::Choice, 2, 0, 0,
          {{"2 (16k)", "2"}, {"3 (24k)", "3"}, {"4 (32k)", "4"}, {"5 (40k)", "5"}}},
      }
    },
    // ADPCM IMA WAV (game audio, fixed 4:1 ratio)
    {
      "adpcm_ima",
      "ADPCM IMA",
      "adpcm_ima_wav",
      "wav",
      "wav",
      0,
      0,
      0,
      1024,
      1024,
      "",
      true,
      false
    },
    // ADPCM Microsoft (classic Windows, fixed ratio)
    {
      "adpcm_ms",
      "ADPCM MS",
      "adpcm_ms",
      "wav",
      "wav",
      0,
      0,
      0,
      1024,
      1024,
      "",
      true,
      false
    },
    // Nellymoser (Flash-era streaming, mono only, max 44100Hz)
    {
      "nellymoser",
      "Nellymoser",
      "nellymoser",
      "flv",
      "flv",
      64,
      16,
      64,
      256,
      256,
      "-ar 44100 -ac 1",
      false,
      false
    },
    //==========================================================================
    // Tier 3: Retro / Novelty / Extra
    //==========================================================================
    // RealAudio 1.0 (1995, 14.4k modem era)
    {
      "ra144",
      "RealAudio 1.0",
      "real_144",
      "rm",
      "rm",
      8,
      8,
      8,
      160,
      160,
      "",
      true,
      false
    },
    // DFPWM (1-bit audio, Minecraft ComputerCraft)
    {
      "dfpwm",
      "DFPWM",
      "dfpwm",
      "dfpwm",
      "dfpwm",
      48,
      48,
      48,
      1024,
      1024,
      "",
      true,
      false
    },
    // WMA v1 (Windows Media Audio 1)
    {
      "wmav1",
      "WMA v1",
      "wmav1",
      "asf",
      "asf",
      128,
      32,
      192,
      2048,
      2048,
      "",
      false,
      false
    },
    // WavPack (lossless)
    {
      "wavpack",
      "WavPack",
      "wavpack",
      "wv",
      "wv",
      0,
      0,
      0,
      4096,
      4096,
      "",
      true,
      false,
      {
        {"wavpack_comp", "Compression", "-compression_level", CodecOptionType::IntRange, 1, 0, 8, {}},
      }
    },
    // ADPCM Yamaha (console / synth)
    {
      "adpcm_yamaha",
      "ADPCM Yamaha",
      "adpcm_yamaha",
      "wav",
      "wav",
      0,
      0,
      0,
      1024,
      1024,
      "",
      true,
      false
    },
  };
}
//==============================================================================
// Detection
//==============================================================================
void CodecRegistry::DetectAvailable(const std::string& ffmpegPath)
{
  std::lock_guard<std::mutex> lock(mMutex);
  DebugLogRegistry("DetectAvailable: running " + ffmpegPath + " -encoders");
  // Run ffmpeg -encoders and capture output
  std::string command = ffmpegPath + " -encoders 2>&1";
  FILE* pipe = _popen(command.c_str(), "r");
  if (!pipe)
  {
    DebugLogRegistry("DetectAvailable: _popen failed");
    return;
  }
  char buffer[1024];
  std::string result;
  while (fgets(buffer, sizeof(buffer), pipe))
    result += buffer;
  _pclose(pipe);
  DebugLogRegistry("DetectAvailable: got " + std::to_string(result.size()) + " bytes of output");
  // Check each codec's encoder name against the output
  for (auto& codec : mCodecs)
  {
    codec.available = (result.find(codec.encoderName) != std::string::npos);
    DebugLogRegistry("  " + codec.displayName + " (" + codec.encoderName + "): " +
                     (codec.available ? "AVAILABLE" : "not found"));
  }
  mDetected = true;

  // Count available codecs inline (mMutex already held, can't call GetAvailableCount())
  int availCount = 0;
  for (const auto& codec : mCodecs)
    if (codec.available) availCount++;
  DebugLogRegistry("DetectAvailable: " + std::to_string(availCount) + " codecs available");
}
//==============================================================================
// Accessors
//==============================================================================
std::vector<const CodecInfo*> CodecRegistry::GetAvailable() const
{
  std::lock_guard<std::mutex> lock(mMutex);
  std::vector<const CodecInfo*> available;
  for (const auto& codec : mCodecs)
  {
    if (codec.available)
      available.push_back(&codec);
  }
  return available;
}
int CodecRegistry::GetAvailableCount() const
{
  std::lock_guard<std::mutex> lock(mMutex);
  int count = 0;
  for (const auto& codec : mCodecs)
  {
    if (codec.available)
      count++;
  }
  return count;
}
const CodecInfo* CodecRegistry::GetAvailableByIndex(int index) const
{
  std::lock_guard<std::mutex> lock(mMutex);
  int current = 0;
  for (const auto& codec : mCodecs)
  {
    if (codec.available)
    {
      if (current == index)
        return &codec;
      current++;
    }
  }
  return nullptr;
}
int CodecRegistry::GetAvailableIndexById(const std::string& id) const
{
  std::lock_guard<std::mutex> lock(mMutex);
  int current = 0;
  for (const auto& codec : mCodecs)
  {
    if (codec.available)
    {
      if (codec.id == id)
        return current;
      current++;
    }
  }
  return -1;
}
const CodecInfo* CodecRegistry::GetById(const std::string& id) const
{
  std::lock_guard<std::mutex> lock(mMutex);
  for (const auto& codec : mCodecs)
  {
    if (codec.id == id)
      return &codec;
  }
  return nullptr;
}