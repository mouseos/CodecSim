//==============================================================================
// CodecProcessor.cpp
// Codec processor implementations for CodecSim (MP3, HE-AAC)
// Copyright 2025 MouseSoft
//==============================================================================

#include "CodecProcessor.h"
#include <algorithm>
#include <sstream>
#include <cstring>

#ifdef _WIN32
#include <windows.h>
#include <debugapi.h>

// Debug logging helper
static void DebugLogCodec(const std::string& msg)
{
  FILE* f = fopen("D:\\ffmpeg_codec_debug.log", "a");
  if (f) {
    fprintf(f, "[CodecProcessor] %s\n", msg.c_str());
    fflush(f);
    fclose(f);
  }
  OutputDebugStringA(("[CodecProcessor] " + msg + "\n").c_str());
}
#else
static void DebugLogCodec(const std::string& msg) { (void)msg; }
#endif

//==============================================================================
// BaseCodecProcessor Implementation
//==============================================================================

BaseCodecProcessor::BaseCodecProcessor()
  : mSampleRate(44100)
  , mChannels(2)
  , mBitrate(128000)
  , mFrameSize(1152)
  , mLatencySamples(0)
  , mInitialized(false)
{
  DebugLogCodec("BaseCodecProcessor constructor called");
  mPipeManager = std::make_unique<FFmpegPipeManager>();
}

BaseCodecProcessor::~BaseCodecProcessor()
{
  DebugLogCodec("BaseCodecProcessor destructor called");
  Shutdown();
}

bool BaseCodecProcessor::Initialize(int sampleRate, int channels)
{
  std::lock_guard<std::recursive_mutex> lock(mMutex);

  DebugLogCodec("BaseCodecProcessor::Initialize() - sampleRate=" + std::to_string(sampleRate) +
                ", channels=" + std::to_string(channels));

  // Shutdown previous instance if running
  Shutdown();

  mSampleRate = sampleRate;
  mChannels = channels;
  mFrameSize = GetDefaultFrameSize();
  // Note: mBitrate is set by SetBitrate() before Initialize() is called.
  // Only use default if no bitrate has been explicitly set.
  if (mBitrate <= 0)
    mBitrate = GetDefaultBitrate() * 1000;

  // Configure FFmpegPipeManager
  FFmpegPipeManager::Config config;
  config.ffmpegPath = "ffmpeg.exe";  // Assume ffmpeg.exe is in PATH
  config.codecName = GetCodecName();
  config.sampleRate = mSampleRate;
  config.channels = mChannels;
  config.bitrate = mBitrate;
  config.additionalArgs = GetAdditionalArgs();
  config.bufferSize = 65536;

  DebugLogCodec("Starting FFmpegPipeManager with codec=" + config.codecName +
                ", bitrate=" + std::to_string(config.bitrate));

  // Start FFmpeg pipe manager
  if (!mPipeManager->Start(config))
  {
    std::string error = mPipeManager->GetLastErrorMessage();
    DebugLogCodec("Failed to start FFmpegPipeManager: " + error);
    return false;
  }

  // Allocate processing buffer
  mProcessBuffer.resize(mFrameSize * mChannels * 2);  // Extra space for safety

  // Set latency estimate (frame size + some pipeline latency)
  mLatencySamples = mFrameSize + 512;

  mInitialized = true;
  DebugLogCodec("BaseCodecProcessor initialized successfully");
  return true;
}

void BaseCodecProcessor::Shutdown()
{
  std::lock_guard<std::recursive_mutex> lock(mMutex);

  DebugLogCodec("BaseCodecProcessor::Shutdown() called");

  if (mPipeManager && mPipeManager->IsRunning())
  {
    mPipeManager->Stop();
  }

  mProcessBuffer.clear();
  mInitialized = false;
}

void BaseCodecProcessor::Reset()
{
  std::lock_guard<std::recursive_mutex> lock(mMutex);

  DebugLogCodec("BaseCodecProcessor::Reset() called");

  if (mInitialized)
  {
    // Flush any remaining data
    if (mPipeManager && mPipeManager->IsRunning())
    {
      mPipeManager->Flush();
    }
  }
}

int BaseCodecProcessor::Encode(const float* input, int numSamples, uint8_t* output, int maxOutputBytes)
{
  std::lock_guard<std::recursive_mutex> lock(mMutex);

  if (!mInitialized || !mPipeManager || !mPipeManager->IsRunning())
    return 0;

  // For pipe-based codec, we don't directly encode
  // Instead, we write samples and read processed output
  // This is handled by the Process() flow in the main audio callback
  return 0;
}

int BaseCodecProcessor::Decode(const uint8_t* input, int inputBytes, float* output, int maxOutputSamples)
{
  std::lock_guard<std::recursive_mutex> lock(mMutex);

  if (!mInitialized || !mPipeManager || !mPipeManager->IsRunning())
    return 0;

  // For pipe-based codec, we don't directly decode
  // FFmpeg handles encode->decode pipeline internally
  return 0;
}

int BaseCodecProcessor::Process(const float* input, int numSamples, float* output, int maxOutputSamples)
{
  if (!mInitialized || !mPipeManager || !mPipeManager->IsRunning())
    return 0;

  // Non-blocking: enqueue input data to worker thread
  mPipeManager->WriteSamples(input, numSamples);

  // Non-blocking: read only available output data (timeout=0)
  size_t samplesRead = mPipeManager->ReadSamples(output, maxOutputSamples, 0);
  return static_cast<int>(samplesRead);
}

int BaseCodecProcessor::GetLatencySamples() const
{
  return mLatencySamples;
}

int BaseCodecProcessor::GetFrameSize() const
{
  return mFrameSize;
}

bool BaseCodecProcessor::IsInitialized() const
{
  return mInitialized;
}

void BaseCodecProcessor::SetLogCallback(std::function<void(const std::string&)> callback)
{
  if (mPipeManager)
  {
    mPipeManager->SetLogCallback(callback);
  }
}

void BaseCodecProcessor::SetBitrate(int bitrateKbps)
{
  std::lock_guard<std::recursive_mutex> lock(mMutex);

  mBitrate = bitrateKbps * 1000;
  DebugLogCodec("SetBitrate called: " + std::to_string(bitrateKbps) + " kbps");

  // Reinitialize if already running
  if (mInitialized)
  {
    int savedSampleRate = mSampleRate;
    int savedChannels = mChannels;
    Initialize(savedSampleRate, savedChannels);
  }
}

void BaseCodecProcessor::SetSampleRate(int sampleRate)
{
  std::lock_guard<std::recursive_mutex> lock(mMutex);

  mSampleRate = sampleRate;
  DebugLogCodec("SetSampleRate called: " + std::to_string(sampleRate) + " Hz");

  // Reinitialize if already running
  if (mInitialized)
  {
    int savedChannels = mChannels;
    Initialize(mSampleRate, savedChannels);
  }
}

//==============================================================================
// MP3CodecProcessor Implementation
//==============================================================================

MP3CodecProcessor::MP3CodecProcessor()
  : BaseCodecProcessor()
{
  DebugLogCodec("MP3CodecProcessor constructor");
  mFrameSize = 1152;  // Standard MP3 frame size
  mLatencySamples = 576;  // Typical MP3 encoder delay
}

void MP3CodecProcessor::SetBitrate(int bitrateKbps)
{
  std::lock_guard<std::recursive_mutex> lock(mMutex);

  // Clamp to MP3 valid range (8-320 kbps)
  int clampedBitrate = std::clamp(bitrateKbps, 8, 320);
  DebugLogCodec("MP3CodecProcessor::SetBitrate: " + std::to_string(clampedBitrate) + " kbps");

  BaseCodecProcessor::SetBitrate(clampedBitrate);
}

std::string MP3CodecProcessor::GetAdditionalArgs() const
{
  // CBR mode: no additional args needed. -b:a is set in the main config.
  return "";
}

int MP3CodecProcessor::GetCodecSampleRate() const
{
  // MP3 supports: 8000, 11025, 12000, 16000, 22050, 24000, 32000, 44100, 48000
  // Use the closest supported rate
  if (mSampleRate <= 8000) return 8000;
  if (mSampleRate <= 11025) return 11025;
  if (mSampleRate <= 12000) return 12000;
  if (mSampleRate <= 16000) return 16000;
  if (mSampleRate <= 22050) return 22050;
  if (mSampleRate <= 24000) return 24000;
  if (mSampleRate <= 32000) return 32000;
  if (mSampleRate <= 44100) return 44100;
  return 48000;
}

//==============================================================================
// HEAACCodecProcessor Implementation
//==============================================================================

HEAACCodecProcessor::HEAACCodecProcessor()
  : BaseCodecProcessor()
  , mProfile(5)  // Default: HE-AAC v1 (AAC-LC + SBR)
  , mUseFdk(false)
{
  DebugLogCodec("HEAACCodecProcessor constructor");
  mFrameSize = 1024;  // Standard AAC frame size (for 48kHz)
  mLatencySamples = 2048;  // HE-AAC has higher latency due to SBR

  // Detect if libfdk_aac is available
  FILE* pipe = _popen("ffmpeg -encoders 2>&1", "r");
  if (pipe)
  {
    char buffer[1024];
    std::string result;
    while (fgets(buffer, sizeof(buffer), pipe))
      result += buffer;
    _pclose(pipe);

    mUseFdk = (result.find("libfdk_aac") != std::string::npos);
    DebugLogCodec("libfdk_aac available: " + std::string(mUseFdk ? "YES" : "NO"));
  }
}

void HEAACCodecProcessor::SetBitrate(int bitrateKbps)
{
  std::lock_guard<std::recursive_mutex> lock(mMutex);

  // Clamp to HE-AAC recommended range (24-128 kbps)
  // HE-AAC is optimized for low bitrates
  int clampedBitrate = std::clamp(bitrateKbps, 24, 128);
  DebugLogCodec("HEAACCodecProcessor::SetBitrate: " + std::to_string(clampedBitrate) + " kbps");

  BaseCodecProcessor::SetBitrate(clampedBitrate);
}

void HEAACCodecProcessor::SetProfile(int profile)
{
  std::lock_guard<std::recursive_mutex> lock(mMutex);

  // Valid profiles for libfdk_aac:
  // 2 = AAC-LC (Low Complexity)
  // 5 = HE-AAC v1 (AAC-LC + SBR)
  // 29 = HE-AAC v2 (AAC-LC + SBR + PS)

  if (profile == 2 || profile == 5 || profile == 29)
  {
    mProfile = profile;
    DebugLogCodec("HEAACCodecProcessor::SetProfile: " + std::to_string(profile));

    // Reinitialize if already running
    if (mInitialized)
    {
      int savedSampleRate = mSampleRate;
      int savedChannels = mChannels;
      Initialize(savedSampleRate, savedChannels);
    }
  }
  else
  {
    DebugLogCodec("HEAACCodecProcessor::SetProfile: invalid profile " + std::to_string(profile));
  }
}

std::string HEAACCodecProcessor::GetAdditionalArgs() const
{
  std::ostringstream oss;

  if (mUseFdk)
  {
    // libfdk_aac specific options
    oss << "-profile:a " << mProfile;
    oss << " -vbr 0";
    oss << " -afterburner 1";
  }
  else
  {
    // Built-in AAC encoder only supports AAC-LC.
    // HE-AAC profiles (aac_he, aac_he_v2) are NOT supported and cause
    // "MPEG-4 AOT 21 is not allowed in ADTS" errors.
    // Fall back to AAC-LC (no profile argument needed, it's the default).
    DebugLogCodec("Built-in AAC encoder: using AAC-LC (HE-AAC not supported without libfdk_aac)");
  }

  return oss.str();
}

//==============================================================================
// CodecProcessorFactory Implementation
//==============================================================================

std::unique_ptr<ICodecProcessor> CodecProcessorFactory::Create(ECodecType codecType)
{
  DebugLogCodec("CodecProcessorFactory::Create() called with codecType=" +
                std::to_string(static_cast<int>(codecType)));

  switch (codecType)
  {
    case ECodecType::MP3:
      DebugLogCodec("Creating MP3CodecProcessor...");
      return std::make_unique<MP3CodecProcessor>();

    case ECodecType::HEAAC:
      DebugLogCodec("Creating HEAACCodecProcessor...");
      return std::make_unique<HEAACCodecProcessor>();

    default:
      DebugLogCodec("Unknown codec type, returning nullptr");
      return nullptr;
  }
}

std::unique_ptr<ICodecProcessor> CodecProcessorFactory::Create(ECodecType codecType, int sampleRate, int channels)
{
  auto processor = Create(codecType);

  if (processor && processor->Initialize(sampleRate, channels))
  {
    DebugLogCodec("CodecProcessorFactory::Create() - initialized successfully");
    return processor;
  }

  DebugLogCodec("CodecProcessorFactory::Create() - initialization failed");
  return nullptr;
}

const char* CodecProcessorFactory::GetCodecName(ECodecType codecType)
{
  switch (codecType)
  {
    case ECodecType::MP3:
      return "MP3 (LAME)";
    case ECodecType::HEAAC:
      return "HE-AAC (FDK)";
    default:
      return "Unknown";
  }
}

bool CodecProcessorFactory::IsCodecAvailable(ECodecType codecType)
{
  // For FFmpegPipeManager-based codecs, we assume ffmpeg.exe is available
  // with required codec support (libmp3lame, libfdk_aac)
  // This could be enhanced to actually check ffmpeg codec availability

  switch (codecType)
  {
    case ECodecType::MP3:
    case ECodecType::HEAAC:
      return true;
    default:
      return false;
  }
}
