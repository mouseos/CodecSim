//==============================================================================
// CodecProcessor.cpp
// Generic codec processor implementation
// Copyright 2025 MouseSoft
//==============================================================================

#include "CodecProcessor.h"
#include <algorithm>
#include <cstring>

#ifdef _WIN32
#include <windows.h>
#include <debugapi.h>

static void DebugLogCodec(const std::string& msg)
{
  FILE* f = fopen("D:\ffmpeg_codec_debug.log", "a");
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
// GenericCodecProcessor Implementation
//==============================================================================

GenericCodecProcessor::GenericCodecProcessor(const CodecInfo& codecInfo)
  : mCodecInfo(codecInfo)
  , mSampleRate(44100)
  , mChannels(2)
  , mBitrate(codecInfo.defaultBitrate * 1000)
  , mFrameSize(codecInfo.frameSize)
  , mLatencySamples(codecInfo.latencySamples)
  , mInitialized(false)
{
  DebugLogCodec("GenericCodecProcessor created for: " + codecInfo.displayName +
                " (encoder=" + codecInfo.encoderName + ")");
  mPipeManager = std::make_unique<FFmpegPipeManager>();
}

GenericCodecProcessor::~GenericCodecProcessor()
{
  DebugLogCodec("GenericCodecProcessor destructor: " + mCodecInfo.displayName);
  Shutdown();
}

bool GenericCodecProcessor::Initialize(int sampleRate, int channels)
{
  std::lock_guard<std::recursive_mutex> lock(mMutex);

  DebugLogCodec("Initialize: " + mCodecInfo.displayName +
                " sampleRate=" + std::to_string(sampleRate) +
                " channels=" + std::to_string(channels) +
                " bitrate=" + std::to_string(mBitrate));

  Shutdown();

  mSampleRate = sampleRate;
  mChannels = channels;

  if (mBitrate <= 0)
    mBitrate = mCodecInfo.defaultBitrate * 1000;

  // Snap bitrate for codecs that require specific values
  if (mCodecInfo.encoderName == "libtwolame")
  {
    // MPEG-1 Layer 2 valid bitrates for stereo (kbps)
    static const int validBitrates[] = {64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 384};
    int bitrateKbps = mBitrate / 1000;
    int best = validBitrates[0];
    int bestDiff = std::abs(bitrateKbps - best);
    for (int vb : validBitrates)
    {
      int diff = std::abs(bitrateKbps - vb);
      if (diff < bestDiff) { best = vb; bestDiff = diff; }
    }
    mBitrate = best * 1000;
    DebugLogCodec("MP2 bitrate snapped to " + std::to_string(best) + " kbps");
  }

  // Configure FFmpegPipeManager
  FFmpegPipeManager::Config config;
  config.ffmpegPath = FFmpegPipeManager::ResolveFFmpegPath();
  config.codecName = mCodecInfo.encoderName;
  config.sampleRate = mSampleRate;
  config.channels = mChannels;
  config.bitrate = mBitrate;
  config.additionalArgs = mCodecInfo.additionalArgs;
  config.muxerFormat = mCodecInfo.muxerFormat;
  config.demuxerFormat = mCodecInfo.demuxerFormat;
  config.bufferSize = 65536;

  DebugLogCodec("Starting FFmpegPipeManager: codec=" + config.codecName +
                " muxer=" + config.muxerFormat + " demuxer=" + config.demuxerFormat);

  if (!mPipeManager->Start(config))
  {
    std::string error = mPipeManager->GetLastErrorMessage();
    DebugLogCodec("Failed to start FFmpegPipeManager: " + error);
    return false;
  }

  mProcessBuffer.resize(mFrameSize * mChannels * 2);
  mLatencySamples = mCodecInfo.latencySamples;
  mInitialized = true;

  DebugLogCodec("Initialized successfully");
  return true;
}

void GenericCodecProcessor::Shutdown()
{
  std::lock_guard<std::recursive_mutex> lock(mMutex);

  if (mPipeManager && mPipeManager->IsRunning())
    mPipeManager->Stop();

  mProcessBuffer.clear();
  mInitialized = false;
}

void GenericCodecProcessor::Reset()
{
  std::lock_guard<std::recursive_mutex> lock(mMutex);

  if (mInitialized && mPipeManager && mPipeManager->IsRunning())
    mPipeManager->Flush();
}

int GenericCodecProcessor::Encode(const float* input, int numSamples, uint8_t* output, int maxOutputBytes)
{
  // Not used - Process() handles the full pipeline
  return 0;
}

int GenericCodecProcessor::Decode(const uint8_t* input, int inputBytes, float* output, int maxOutputSamples)
{
  // Not used - Process() handles the full pipeline
  return 0;
}

int GenericCodecProcessor::Process(const float* input, int numSamples, float* output, int maxOutputSamples)
{
  if (!mInitialized || !mPipeManager || !mPipeManager->IsRunning())
    return 0;

  mPipeManager->WriteSamples(input, numSamples);
  size_t samplesRead = mPipeManager->ReadSamples(output, maxOutputSamples, 0);
  return static_cast<int>(samplesRead);
}

int GenericCodecProcessor::GetLatencySamples() const
{
  return mLatencySamples;
}

int GenericCodecProcessor::GetFrameSize() const
{
  return mFrameSize;
}

bool GenericCodecProcessor::IsInitialized() const
{
  return mInitialized;
}

void GenericCodecProcessor::SetLogCallback(std::function<void(const std::string&)> callback)
{
  if (mPipeManager)
    mPipeManager->SetLogCallback(callback);
}

void GenericCodecProcessor::SetBitrate(int bitrateKbps)
{
  std::lock_guard<std::recursive_mutex> lock(mMutex);

  int clamped = std::clamp(bitrateKbps, mCodecInfo.minBitrate, mCodecInfo.maxBitrate);
  mBitrate = clamped * 1000;
  DebugLogCodec("SetBitrate: " + std::to_string(clamped) + " kbps (clamped from " +
                std::to_string(bitrateKbps) + ")");

  if (mInitialized)
  {
    int savedSR = mSampleRate;
    int savedCh = mChannels;
    Initialize(savedSR, savedCh);
  }
}

void GenericCodecProcessor::SetSampleRate(int sampleRate)
{
  std::lock_guard<std::recursive_mutex> lock(mMutex);

  mSampleRate = sampleRate;
  DebugLogCodec("SetSampleRate: " + std::to_string(sampleRate));

  if (mInitialized)
  {
    int savedCh = mChannels;
    Initialize(mSampleRate, savedCh);
  }
}

void GenericCodecProcessor::SetAdditionalArgs(const std::string& args)
{
  std::lock_guard<std::recursive_mutex> lock(mMutex);
  mCodecInfo.additionalArgs = args;
}
