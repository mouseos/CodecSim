#include "CodecSim.h"
#include "IPlug_include_in_plug_src.h"
#include "CodecProcessor.h"

#if IPLUG_EDITOR
#include "IControls.h"
#endif

//==============================================================================
// Debug logging helper - writes to file and OutputDebugStringA
//==============================================================================
#ifdef _WIN32
#include <windows.h>
#include <debugapi.h>
#include <cstdio>

static void DebugLogCodecSim(const std::string& msg)
{
  FILE* f = fopen("D:\\ffmpeg_debug.log", "a");
  if (f) {
    fprintf(f, "[CodecSim] %s\n", msg.c_str());
    fflush(f);
    fclose(f);
  }
  OutputDebugStringA(("[CodecSim] " + msg + "\n").c_str());
}
#else
static void DebugLogCodecSim(const std::string& msg) { (void)msg; }
#endif

//==============================================================================
// Color Definitions
//==============================================================================
namespace Colors
{
  const IColor Background(255, 30, 30, 30);           // Darker background
  const IColor Panel(255, 45, 45, 45);                // Panel background
  const IColor PanelBorder(255, 80, 80, 80);          // Panel border
  const IColor AccentBlue(255, 100, 180, 255);        // Brighter blue for sliders
  const IColor AccentBlueDark(255, 50, 120, 200);     // Track color
  const IColor TextWhite(255, 255, 255, 255);         // Pure white text
  const IColor TextGray(255, 200, 200, 200);          // Lighter gray for labels
  const IColor SliderHandle(255, 255, 255, 255);      // White slider handle
}

//==============================================================================
// UI Layout Constants
//==============================================================================
namespace Layout
{
  // Window size: 600x400 (defined in config.h)
  constexpr float Padding = 10.f;
  constexpr float TitleBarHeight = 40.f;

  // Main panel (left side)
  constexpr float MainPanelWidth = 280.f;

  // Detail panel (right side)
  constexpr float DetailPanelWidth = 290.f;

  // Control dimensions
  constexpr float SliderHeight = 30.f;
  constexpr float LabelHeight = 20.f;
  constexpr float ControlSpacing = 15.f;
  constexpr float SectionSpacing = 25.f;

  // Codec selector
  constexpr float CodecSelectorHeight = 35.f;
}

//==============================================================================
// Constructor
//==============================================================================
CodecSim::CodecSim(const InstanceInfo& info)
: iplug::Plugin(info, MakeConfig(kNumParams, kNumPresets))
, mCurrentCodec(ECodecType::MP3)
, mSampleRate(44100)
, mNumChannels(2)
, mLatencySamples(0)
, mActualBitrate(128)
, mCodecParam(0)
, mBitrateParam(128)
{
  DebugLogCodecSim("Constructor - START");
  //============================================================================
  // Initialize Common Parameters
  //============================================================================

  // Codec selection (MP3, HE-AAC)
  GetParam(kParamCodec)->InitEnum("Codec", 0, 2, "", IParam::kFlagsNone, "",
    "MP3", "HE-AAC");

  // Bitrate (16-510 kbps, default 128)
  // Note: MP3 supports 8-320 kbps, HE-AAC supports 24-128 kbps (recommended)
  GetParam(kParamBitrate)->InitInt("Bitrate", 128, 16, 510, "kbps");

  // Sample rate selection (44100 Hz, 48000 Hz, 96000 Hz)
  GetParam(kParamSampleRate)->InitEnum("Sample Rate", 1, 3, "", IParam::kFlagsNone, "",
    "44100 Hz", "48000 Hz", "96000 Hz");

  // Start/Stop toggle (default: off - user must click Start)
  GetParam(kParamEnabled)->InitBool("Enabled", false);

#if IPLUG_EDITOR
  mMakeGraphicsFunc = [&]() {
    return MakeGraphics(*this, PLUG_WIDTH, PLUG_HEIGHT, PLUG_FPS);
  };

  mLayoutFunc = [&](IGraphics* pGraphics) {
    if (!pGraphics) return;

    const IRECT bounds = pGraphics->GetBounds();

    //==========================================================================
    // Handle resize (if controls already exist)
    //==========================================================================
    if (pGraphics->NControls()) {
      pGraphics->GetBackgroundControl()->SetTargetAndDrawRECTs(bounds);
      return;
    }

    //==========================================================================
    // Initial Setup
    //==========================================================================
    pGraphics->LoadFont("Roboto-Regular", ROBOTO_FN);
    pGraphics->AttachPanelBackground(Colors::Background);

    //==========================================================================
    // Create custom IVStyle for controls with high contrast
    //==========================================================================
    const IVStyle sliderStyle = IVStyle({
      IColor(255, 45, 45, 45),          // Background
      IColor(255, 70, 70, 70),          // Foreground (track)
      IColor(255, 100, 180, 255),       // Pressed (handle/active - bright blue)
      IColor(255, 90, 90, 90),          // Frame
      IColor(255, 130, 200, 255),       // Highlight (hover)
      IColor(255, 30, 30, 30),          // Shadow
      Colors::TextWhite,                // X1 (text color)
      Colors::TextWhite,                // X2
      Colors::TextWhite                 // X3
    }).WithLabelText(IText(13.f, Colors::TextWhite, "Roboto-Regular"))
      .WithValueText(IText(13.f, Colors::TextWhite, "Roboto-Regular"))
      .WithDrawFrame(true)
      .WithRoundness(0.2f);

    const IVStyle switchStyle = IVStyle({
      IColor(255, 45, 45, 45),          // Background
      IColor(255, 80, 80, 80),          // Foreground (off state)
      IColor(255, 100, 180, 255),       // Pressed (on state - bright blue)
      IColor(255, 90, 90, 90),          // Frame
      IColor(255, 130, 200, 255),       // Highlight
      IColor(255, 30, 30, 30),          // Shadow
      Colors::TextWhite,                // X1
      Colors::TextWhite,                // X2
      Colors::TextWhite                 // X3
    }).WithLabelText(IText(13.f, Colors::TextWhite, "Roboto-Regular"))
      .WithValueText(IText(13.f, Colors::TextWhite, "Roboto-Regular"))
      .WithDrawFrame(true)
      .WithRoundness(0.2f);

    const IVStyle tabStyle = IVStyle({
      IColor(255, 55, 55, 55),          // Background (unselected)
      IColor(255, 75, 75, 75),          // Foreground
      IColor(255, 100, 180, 255),       // Pressed (selected - bright blue)
      IColor(255, 90, 90, 90),          // Frame
      IColor(255, 100, 100, 100),       // Highlight (hover)
      IColor(255, 30, 30, 30),          // Shadow
      Colors::TextWhite,                // X1
      Colors::TextWhite,                // X2
      Colors::TextWhite                 // X3
    }).WithLabelText(IText(12.f, Colors::TextWhite, "Roboto-Regular"))
      .WithValueText(IText(12.f, Colors::TextWhite, "Roboto-Regular"))
      .WithDrawFrame(true)
      .WithRoundness(0.1f);

    //==========================================================================
    // Title Bar
    //==========================================================================
    const IRECT titleBarBounds = bounds.GetFromTop(Layout::TitleBarHeight);
    const IRECT titleTextBounds = titleBarBounds.GetFromLeft(200.f).GetPadded(-Layout::Padding);

    // Title
    pGraphics->AttachControl(
      new ITextControl(titleTextBounds, "CodecSim",
        IText(24.f, Colors::TextWhite, "Roboto-Regular", EAlign::Near, EVAlign::Middle)),
      kCtrlTagTitle
    );

    //==========================================================================
    // Main Panel (Left Side)
    //==========================================================================
    const IRECT contentArea = bounds.GetReducedFromTop(Layout::TitleBarHeight).GetPadded(-Layout::Padding);
    const IRECT mainPanelBounds = contentArea.GetFromLeft(Layout::MainPanelWidth);

    // Main panel background
    pGraphics->AttachControl(new IPanelControl(mainPanelBounds, Colors::Panel));

    // Panel inner area
    const IRECT mainPanelInner = mainPanelBounds.GetPadded(-Layout::Padding);
    float yPos = mainPanelInner.T;

    // Section: Codec Selection
    const IRECT codecLabelBounds = IRECT(mainPanelInner.L, yPos, mainPanelInner.R, yPos + Layout::LabelHeight);
    pGraphics->AttachControl(new ITextControl(codecLabelBounds, "Codec",
      IText(14.f, Colors::TextGray, "Roboto-Regular", EAlign::Near)));
    yPos += Layout::LabelHeight + 5.f;

    const IRECT codecSelectorBounds = IRECT(mainPanelInner.L, yPos, mainPanelInner.R, yPos + Layout::CodecSelectorHeight);
    pGraphics->AttachControl(
      new IVTabSwitchControl(codecSelectorBounds, kParamCodec, {"MP3", "HE-AAC"}, "", tabStyle),
      kCtrlTagCodecSelector
    );
    yPos += Layout::CodecSelectorHeight + Layout::SectionSpacing;

    // Section: Bitrate
    const IRECT bitrateLabelBounds = IRECT(mainPanelInner.L, yPos, mainPanelInner.R, yPos + Layout::LabelHeight);
    pGraphics->AttachControl(new ITextControl(bitrateLabelBounds, "Bitrate (kbps)",
      IText(14.f, Colors::TextGray, "Roboto-Regular", EAlign::Near)));
    yPos += Layout::LabelHeight + 5.f;

    const IRECT bitrateSliderBounds = IRECT(mainPanelInner.L, yPos, mainPanelInner.R, yPos + Layout::SliderHeight);
    pGraphics->AttachControl(
      new IVSliderControl(bitrateSliderBounds, kParamBitrate, "", sliderStyle, true, EDirection::Horizontal),
      kCtrlTagBitrateSlider
    );
    yPos += Layout::SliderHeight + Layout::SectionSpacing;

    // Section: Sample Rate
    const IRECT sampleRateLabelBounds = IRECT(mainPanelInner.L, yPos, mainPanelInner.R, yPos + Layout::LabelHeight);
    pGraphics->AttachControl(new ITextControl(sampleRateLabelBounds, "Sample Rate",
      IText(14.f, Colors::TextGray, "Roboto-Regular", EAlign::Near)));
    yPos += Layout::LabelHeight + 5.f;

    const IRECT sampleRateSelectorBounds = IRECT(mainPanelInner.L, yPos, mainPanelInner.R, yPos + Layout::CodecSelectorHeight);
    pGraphics->AttachControl(
      new IVTabSwitchControl(sampleRateSelectorBounds, kParamSampleRate, {"44.1k", "48k", "96k"}, "", tabStyle),
      kCtrlTagSampleRateSelector
    );
    yPos += Layout::CodecSelectorHeight + Layout::SectionSpacing;

    // Section: Start/Stop Button
    const IRECT startStopBounds = IRECT(mainPanelInner.L, yPos, mainPanelInner.R, yPos + Layout::CodecSelectorHeight + 5.f);
    pGraphics->AttachControl(
      new IVToggleControl(startStopBounds, kParamEnabled, "Start", tabStyle, "Stop", "Start"),
      kCtrlTagStartStopButton
    );
    yPos += Layout::CodecSelectorHeight + 10.f;

    //==========================================================================
    // Detail Settings Panel (Right Side)
    //==========================================================================
    const IRECT detailPanelBounds = contentArea.GetFromRight(Layout::DetailPanelWidth);

    // Detail panel background
    pGraphics->AttachControl(new IPanelControl(detailPanelBounds, Colors::Panel));

    // Panel inner area
    const IRECT detailPanelInner = detailPanelBounds.GetPadded(-Layout::Padding);

    // Detail Panel Title
    const IRECT detailTitleBounds = IRECT(detailPanelInner.L, detailPanelInner.T,
                                          detailPanelInner.R, detailPanelInner.T + Layout::LabelHeight);
    pGraphics->AttachControl(new ITextControl(detailTitleBounds, "FFmpeg Log",
      IText(16.f, Colors::TextWhite, "Roboto-Regular", EAlign::Center)));

    // Log display area
    const IRECT logDisplayBounds = IRECT(detailPanelInner.L, detailPanelInner.T + Layout::LabelHeight + 5.f,
                                         detailPanelInner.R, detailPanelInner.B);
    pGraphics->AttachControl(
      new IMultiLineTextControl(logDisplayBounds, "Press Start to begin...",
        IText(10.f, Colors::TextGray, "Roboto-Regular", EAlign::Near, EVAlign::Top)),
      kCtrlTagLogDisplay
    );

  };
#endif

  mConstructed = true;
  DebugLogCodecSim("Constructor - END");
}

CodecSim::~CodecSim()
{
  std::lock_guard<std::recursive_mutex> lock(mCodecMutex);
  if (mCodecProcessor) {
    mCodecProcessor->Shutdown();
    mCodecProcessor.reset();
  }
}

void CodecSim::OnReset()
{
  DebugLogCodecSim("OnReset START");

  // Update sample rate from parameter
  int sampleRateIndex = GetParam(kParamSampleRate)->Int();
  switch (sampleRateIndex) {
    case 0: mSampleRate = 44100; break;
    case 1: mSampleRate = 48000; break;
    case 2: mSampleRate = 96000; break;
    default: mSampleRate = 48000; break;
  }
  mNumChannels = 2;

  // Only reinitialize if currently enabled
  bool enabled = GetParam(kParamEnabled)->Bool();
  if (enabled)
  {
    std::lock_guard<std::recursive_mutex> lock(mCodecMutex);
    InitializeCodec(mCurrentCodec);
  }

  DebugLogCodecSim("OnReset END");
}

void CodecSim::OnParamChange(int paramIdx)
{
  if (!mConstructed) return;

  DebugLogCodecSim("OnParamChange: paramIdx=" + std::to_string(paramIdx));
  switch (paramIdx) {
    case kParamCodec:
    {
      mCurrentCodec = static_cast<ECodecType>(GetParam(kParamCodec)->Int());
      DebugLogCodecSim("OnParamChange: Codec changed to " + std::to_string(static_cast<int>(mCurrentCodec)));
      // Don't auto-reinitialize - user must press Start
      AddLogMessage("Codec changed. Press Start to apply.");
    }
    break;
    case kParamBitrate:
    {
      int bitrateKbps = GetParam(kParamBitrate)->Int();
      DebugLogCodecSim("OnParamChange: Bitrate changed to " + std::to_string(bitrateKbps) + " kbps");
      // Don't auto-reinitialize - user must press Start
      AddLogMessage("Bitrate: " + std::to_string(bitrateKbps) + " kbps. Press Start to apply.");
    }
    break;
    case kParamSampleRate:
    {
      int sampleRateIndex = GetParam(kParamSampleRate)->Int();
      switch (sampleRateIndex) {
        case 0: mSampleRate = 44100; break;
        case 1: mSampleRate = 48000; break;
        case 2: mSampleRate = 96000; break;
        default: mSampleRate = 48000; break;
      }
      DebugLogCodecSim("OnParamChange: Sample rate changed to " + std::to_string(mSampleRate));
      AddLogMessage("Sample rate: " + std::to_string(mSampleRate) + " Hz. Press Start to apply.");
    }
    break;
    case kParamEnabled:
    {
      bool enabled = GetParam(kParamEnabled)->Bool();
      mEnabled.store(enabled, std::memory_order_relaxed);
      DebugLogCodecSim("OnParamChange: Enabled changed to " + std::to_string(enabled));
      if (enabled)
      {
        mCurrentCodec = static_cast<ECodecType>(GetParam(kParamCodec)->Int());
        AddLogMessage("Starting codec...");
        InitializeCodec(mCurrentCodec);
      }
      else
      {
        AddLogMessage("Stopping codec...");
        StopCodec();
      }
    }
    break;
    default:
      break;
  }
}

void CodecSim::OnIdle()
{
  if (GetUI())
  {
    // Update log display
    if (IControl* pLogCtrl = GetUI()->GetControlWithTag(kCtrlTagLogDisplay))
    {
      std::string logText;
      {
        std::lock_guard<std::mutex> lock(mLogMutex);
        for (const auto& line : mLogMessages)
        {
          logText += line + "\n";
        }
      }
      if (!logText.empty())
      {
        pLogCtrl->As<IMultiLineTextControl>()->SetStr(logText.c_str());
        pLogCtrl->SetDirty(false);
      }
    }
  }
}

void CodecSim::ProcessBlock(sample** inputs, sample** outputs, int nFrames)
{
  const int nChans = 2;

  // When not enabled (Stop state), output silence
  if (!mEnabled.load(std::memory_order_relaxed))
  {
    for (int s = 0; s < nFrames; s++)
      for (int c = 0; c < nChans; c++)
        outputs[c][s] = 0.0;
    return;
  }

  std::unique_lock<std::recursive_mutex> lock(mCodecMutex, std::try_to_lock);

  if (!lock.owns_lock() || !mCodecProcessor || !mCodecProcessor->IsInitialized())
  {
    // Codec initializing or not ready - output silence
    for (int s = 0; s < nFrames; s++)
      for (int c = 0; c < nChans; c++)
        outputs[c][s] = 0.0;
    return;
  }

  // Interleave input (no allocation - uses pre-allocated buffers)
  const int totalSamples = nFrames * nChans;
  float* inBuf = mInterleavedInput.data();
  float* outBuf = mInterleavedOutput.data();

  for (int s = 0; s < nFrames; s++)
  {
    inBuf[s * 2]     = static_cast<float>(inputs[0][s]);
    inBuf[s * 2 + 1] = static_cast<float>(inputs[1][s]);
  }

  // Non-blocking Process: enqueue input and retrieve available output
  int processedSamples = mCodecProcessor->Process(
    inBuf, nFrames,
    outBuf, nFrames
  );

  // Write output buffer
  for (int s = 0; s < nFrames; s++)
  {
    if (s < processedSamples)
    {
      outputs[0][s] = static_cast<sample>(outBuf[s * 2]);
      outputs[1][s] = static_cast<sample>(outBuf[s * 2 + 1]);
    }
    else
    {
      // Not enough output yet - output silence (not dry signal)
      outputs[0][s] = 0.0;
      outputs[1][s] = 0.0;
    }
  }
}

void CodecSim::InitializeCodec(ECodecType codecType)
{
  std::lock_guard<std::recursive_mutex> lock(mCodecMutex);

  if (mIsInitializing) {
    DebugLogCodecSim("InitializeCodec - SKIPPED (already initializing, re-entrant call blocked)");
    return;
  }
  mIsInitializing = true;

  DebugLogCodecSim("InitializeCodec START");

  if (mCodecProcessor) {
    mCodecProcessor->Shutdown();
    mCodecProcessor.reset();
  }

  // Create codec processor (uses ffmpeg.exe via pipe, no DLL loading needed)
  mCodecProcessor = CodecProcessorFactory::Create(codecType);

  if (mCodecProcessor) {
    // Connect FFmpeg's stderr output to our UI log
    mCodecProcessor->SetLogCallback([this](const std::string& msg) {
      AddLogMessage("[ffmpeg] " + msg);
    });

    // Apply the current parameters from the UI to the processor object
    UpdateCodecParameters();

    // Now perform the real initialization (launches ffmpeg.exe process) using those parameters
    if (mCodecProcessor->Initialize(mSampleRate, mNumChannels)) {
      int latency = mCodecProcessor->GetLatencySamples();
      SetLatency(latency);
      mLatencySamples.store(latency);

      // Pre-allocate interleaved buffers for ProcessBlock (avoid audio thread allocation)
      // Use a generous size: 8192 frames * 2 channels should cover any block size
      const int maxFrames = 8192;
      mInterleavedInput.resize(maxFrames * mNumChannels);
      mInterleavedOutput.resize(maxFrames * mNumChannels);

      AddLogMessage("FFmpeg started: " + std::string(codecType == ECodecType::MP3 ? "MP3" : "HE-AAC")
                   + " @ " + std::to_string(GetParam(kParamBitrate)->Int()) + "kbps, "
                   + std::to_string(mSampleRate) + "Hz");
      DebugLogCodecSim("InitializeCodec - processor initialized successfully");
    } else {
      AddLogMessage("ERROR: FFmpeg initialization failed!");
      DebugLogCodecSim("InitializeCodec - processor.Initialize() FAILED");
    }
  } else {
    DebugLogCodecSim("InitializeCodec - CodecProcessorFactory::Create() returned nullptr");
  }
  DebugLogCodecSim("InitializeCodec END");
  mIsInitializing = false;
}

void CodecSim::StopCodec()
{
  std::lock_guard<std::recursive_mutex> lock(mCodecMutex);

  DebugLogCodecSim("StopCodec START");

  if (mCodecProcessor) {
    mCodecProcessor->Shutdown();
    mCodecProcessor.reset();
  }

  AddLogMessage("Codec stopped.");
  DebugLogCodecSim("StopCodec END");
}

void CodecSim::AddLogMessage(const std::string& msg)
{
  std::lock_guard<std::mutex> lock(mLogMutex);
  mLogMessages.push_back(msg);
  // Keep only last kMaxLogLines messages
  while (static_cast<int>(mLogMessages.size()) > kMaxLogLines)
    mLogMessages.erase(mLogMessages.begin());
}

void CodecSim::UpdateCodecParameters()
{
  if (!mCodecProcessor) return;

  int bitrateKbps = GetParam(kParamBitrate)->Int();

  DebugLogCodecSim("UpdateCodecParameters: bitrateKbps=" + std::to_string(bitrateKbps) +
                   ", codec=" + std::to_string(static_cast<int>(mCurrentCodec)));

  switch (mCurrentCodec)
  {
    case ECodecType::MP3:
      ApplyMP3Parameters(mCodecProcessor.get(), bitrateKbps);
      break;
    case ECodecType::HEAAC:
      ApplyHEAACParameters(mCodecProcessor.get(), bitrateKbps);
      break;
    default:
      break;
  }
}
