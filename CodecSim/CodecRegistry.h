#pragma once
//==============================================================================
// CodecRegistry.h
// Dynamic codec detection and registry for CodecSim
// Copyright 2025 MouseSoft
//==============================================================================
#include <string>
#include <vector>
#include <mutex>
//==============================================================================
// CodecOptionDef - describes a configurable codec option
//==============================================================================
enum class CodecOptionType
{
  Toggle,      // bool: on/off
  Choice,      // enum: dropdown/tab switch
  IntRange     // integer range: number box
};

struct CodecOptionChoice
{
  std::string label;    // Display text: "VoIP", "Audio", etc.
  std::string argValue; // ffmpeg arg value: "voip", "audio", etc.
};

struct CodecOptionDef
{
  std::string key;                        // Unique identifier
  std::string label;                      // UI display label
  std::string argName;                    // ffmpeg arg name (e.g., "-application")
  CodecOptionType type;
  int defaultValue;                       // Default index (Choice/Toggle) or int value (IntRange)
  int minValue;                           // For IntRange only
  int maxValue;                           // For IntRange only
  std::vector<CodecOptionChoice> choices; // For Choice type only
};

//==============================================================================
// CodecInfo - describes a single codec configuration
//==============================================================================
struct CodecInfo
{
  std::string id;             // Internal identifier: "mp3", "aac", "opus", etc.
  std::string displayName;    // UI display name: "MP3", "AAC (LC)", "Opus", etc.
  std::string encoderName;    // ffmpeg encoder name: "libmp3lame", "aac", "libopus"
  std::string muxerFormat;    // ffmpeg -f for encoder output: "mp3", "adts", "ogg"
  std::string demuxerFormat;  // ffmpeg -f for decoder input: "mp3", "aac", "ogg"
  int defaultBitrate;         // Default bitrate in kbps
  int minBitrate;             // Minimum bitrate in kbps
  int maxBitrate;             // Maximum bitrate in kbps
  int frameSize;              // Codec frame size in samples
  int latencySamples;         // Estimated latency in samples
  std::string additionalArgs; // Extra ffmpeg encoder arguments
  bool isLossless;            // If true, bitrate control is disabled
  bool available;             // Detected at runtime via ffmpeg -encoders
  std::vector<CodecOptionDef> options;    // Codec-specific configurable options
};
//==============================================================================
// CodecRegistry - singleton registry of all supported codecs
//==============================================================================
class CodecRegistry
{
public:
  static CodecRegistry& Instance();
  // Detect available codecs by running ffmpeg -encoders
  // Should be called once at startup
  void DetectAvailable(const std::string& ffmpegPath = "ffmpeg.exe");
  // Get all registered codecs (including unavailable)
  const std::vector<CodecInfo>& GetAll() const { return mCodecs; }
  // Get only available codecs
  std::vector<const CodecInfo*> GetAvailable() const;
  // Get available codec count
  int GetAvailableCount() const;
  // Get codec by index into available-only list (0-based)
  const CodecInfo* GetAvailableByIndex(int index) const;
  // Get available codec index by internal id (-1 if not found)
  int GetAvailableIndexById(const std::string& id) const;
  // Get codec by internal id
  const CodecInfo* GetById(const std::string& id) const;
  // Check if detection has been performed
  bool IsDetected() const { return mDetected; }
private:
  CodecRegistry();
  ~CodecRegistry() = default;
  CodecRegistry(const CodecRegistry&) = delete;
  CodecRegistry& operator=(const CodecRegistry&) = delete;
  void RegisterBuiltinCodecs();
  std::vector<CodecInfo> mCodecs;
  bool mDetected = false;
  mutable std::mutex mMutex;
};