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
      false               // available (detected later)
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
      false
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
      false
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
      false
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
      false
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
      false
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
      false
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
      false
    },
    // GSM 06.10
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
      "",
      false,
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