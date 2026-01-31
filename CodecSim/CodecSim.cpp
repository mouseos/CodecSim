#include "CodecSim.h"
#include "IPlug_include_in_plug_src.h"
#include "CodecProcessor.h"
#include "CodecRegistry.h"

#if IPLUG_EDITOR
#include "IControls.h"
#endif

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
  const IColor Background(255, 30, 30, 30);
  const IColor Panel(255, 45, 45, 45);
  const IColor PanelBorder(255, 80, 80, 80);
  const IColor AccentBlue(255, 100, 180, 255);
  const IColor AccentBlueDark(255, 50, 120, 200);
  const IColor TextWhite(255, 255, 255, 255);
  const IColor TextGray(255, 200, 200, 200);
  const IColor SliderHandle(255, 255, 255, 255);
}

//==============================================================================
// UI Layout Constants
//==============================================================================
namespace Layout
{
  constexpr float Padding = 10.f;
  constexpr float TitleBarHeight = 40.f;
  constexpr float MainPanelWidth = 280.f;
  constexpr float DetailPanelWidth = 290.f;
  constexpr float SliderHeight = 30.f;
  constexpr float LabelHeight = 20.f;
  constexpr float ControlSpacing = 15.f;
  constexpr float SectionSpacing = 25.f;
  constexpr float CodecSelectorHeight = 35.f;
}

//==============================================================================
// Bitrate & Sample Rate Presets
//==============================================================================
static const int kBitratePresets[] = {32, 48, 64, 96, 128, 160, 192, 256, 320};
static const int kNumBitratePresets = sizeof(kBitratePresets) / sizeof(kBitratePresets[0]);
// Index kNumBitratePresets = "Other" (custom input)

static const int kSampleRatePresets[] = {8000, 16000, 22050, 32000, 44100, 48000, 88200, 96000};
static const int kNumSampleRatePresets = sizeof(kSampleRatePresets) / sizeof(kSampleRatePresets[0]);

//==============================================================================
// Constructor
//==============================================================================
CodecSim::CodecSim(const InstanceInfo& info)
: iplug::Plugin(info, MakeConfig(kNumParams, kNumPresets))
, mCurrentCodecIndex(0)
, mSampleRate(48000)
, mNumChannels(2)
, mLatencySamples(0)
{
  DebugLogCodecSim("Constructor - START");

  // Detect available codecs from ffmpeg
  CodecRegistry::Instance().DetectAvailable();

  auto availableCodecs = CodecRegistry::Instance().GetAvailable();
  int numAvailable = static_cast<int>(availableCodecs.size());
  DebugLogCodecSim("Available codecs: " + std::to_string(numAvailable));

  if (numAvailable == 0)
  {
    DebugLogCodecSim("WARNING: No codecs available! Is ffmpeg in PATH?");
    numAvailable = 1;
  }

  // Initialize codec selection parameter
  GetParam(kParamCodec)->InitEnum("Codec", 0, numAvailable);
  for (int i = 0; i < static_cast<int>(availableCodecs.size()); i++)
    GetParam(kParamCodec)->SetDisplayText(i, availableCodecs[i]->displayName.c_str());

  // Bitrate preset selector
  GetParam(kParamBitrate)->InitEnum("Bitrate", 4, kNumBitratePresets + 1);
  for (int i = 0; i < kNumBitratePresets; i++)
  {
    char label[32];
    snprintf(label, sizeof(label), "%d kbps", kBitratePresets[i]);
    GetParam(kParamBitrate)->SetDisplayText(i, label);
  }
  GetParam(kParamBitrate)->SetDisplayText(kNumBitratePresets, "Other");

  // Custom bitrate (used when "Other" is selected)
  GetParam(kParamBitrateCustom)->InitInt("Bitrate (Custom)", 128, 8, 640, "kbps");

  // Sample rate selection
  GetParam(kParamSampleRate)->InitEnum("Sample Rate", 5, kNumSampleRatePresets);
  for (int i = 0; i < kNumSampleRatePresets; i++)
  {
    char label[32];
    if (kSampleRatePresets[i] >= 1000)
      snprintf(label, sizeof(label), "%g kHz", kSampleRatePresets[i] / 1000.0);
    else
      snprintf(label, sizeof(label), "%d Hz", kSampleRatePresets[i]);
    GetParam(kParamSampleRate)->SetDisplayText(i, label);
  }

  // Start/Stop toggle
  GetParam(kParamEnabled)->InitBool("Enabled", false);

#if IPLUG_EDITOR
  mMakeGraphicsFunc = [&]() {
    return MakeGraphics(*this, PLUG_WIDTH, PLUG_HEIGHT, PLUG_FPS);
  };

  mLayoutFunc = [&](IGraphics* pGraphics) {
    if (!pGraphics) return;

    const IRECT bounds = pGraphics->GetBounds();

    if (pGraphics->NControls()) {
      pGraphics->GetBackgroundControl()->SetTargetAndDrawRECTs(bounds);
      return;
    }

    pGraphics->LoadFont("Roboto-Regular", ROBOTO_FN);
    pGraphics->AttachPanelBackground(Colors::Background);

    // Styles
    const IVStyle sliderStyle = IVStyle({
      IColor(255, 45, 45, 45),
      IColor(255, 70, 70, 70),
      IColor(255, 100, 180, 255),
      IColor(255, 90, 90, 90),
      IColor(255, 130, 200, 255),
      IColor(255, 30, 30, 30),
      Colors::TextWhite,
      Colors::TextWhite,
      Colors::TextWhite
    }).WithLabelText(IText(13.f, Colors::TextWhite, "Roboto-Regular"))
      .WithValueText(IText(13.f, Colors::TextWhite, "Roboto-Regular"))
      .WithDrawFrame(true)
      .WithRoundness(0.2f);

    const IVStyle tabStyle = IVStyle({
      IColor(255, 55, 55, 55),
      IColor(255, 75, 75, 75),
      IColor(255, 100, 180, 255),
      IColor(255, 90, 90, 90),
      IColor(255, 100, 100, 100),
      IColor(255, 30, 30, 30),
      Colors::TextWhite,
      Colors::TextWhite,
      Colors::TextWhite
    }).WithLabelText(IText(12.f, Colors::TextWhite, "Roboto-Regular"))
      .WithValueText(IText(12.f, Colors::TextWhite, "Roboto-Regular"))
      .WithDrawFrame(true)
      .WithRoundness(0.1f);

    //==========================================================================
    // Title Bar
    //==========================================================================
    const IRECT titleBarBounds = bounds.GetFromTop(Layout::TitleBarHeight);
    const IRECT titleTextBounds = titleBarBounds.GetFromLeft(200.f).GetPadded(-Layout::Padding);

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

    pGraphics->AttachControl(new IPanelControl(mainPanelBounds, Colors::Panel));

    const IRECT mainPanelInner = mainPanelBounds.GetPadded(-Layout::Padding);
    float yPos = mainPanelInner.T;

    // Section: Codec Selection
    const IRECT codecLabelBounds = IRECT(mainPanelInner.L, yPos, mainPanelInner.R, yPos + Layout::LabelHeight);
    pGraphics->AttachControl(new ITextControl(codecLabelBounds, "Codec",
      IText(14.f, Colors::TextGray, "Roboto-Regular", EAlign::Near)));
    yPos += Layout::LabelHeight + 5.f;

    // Build codec name list for selector
    auto codecList = CodecRegistry::Instance().GetAvailable();
    std::vector<const char*> codecNames;
    for (const auto* c : codecList)
      codecNames.push_back(c->displayName.c_str());

    if (codecNames.empty())
      codecNames.push_back("(none)");

    const IRECT codecSelectorBounds = IRECT(mainPanelInner.L, yPos, mainPanelInner.R, yPos + Layout::CodecSelectorHeight);

    if (codecNames.size() <= 4)
    {
      pGraphics->AttachControl(
        new IVTabSwitchControl(codecSelectorBounds, kParamCodec, codecNames, "", tabStyle),
        kCtrlTagCodecSelector
      );
    }
    else
    {
      // For more than 4 codecs, use a dropdown menu
      pGraphics->AttachControl(
        new IVMenuButtonControl(codecSelectorBounds, kParamCodec, "", tabStyle),
        kCtrlTagCodecSelector
      );
    }
    yPos += Layout::CodecSelectorHeight + Layout::SectionSpacing;

    // Section: Bitrate
    const IRECT bitrateLabelBounds = IRECT(mainPanelInner.L, yPos, mainPanelInner.R, yPos + Layout::LabelHeight);
    pGraphics->AttachControl(new ITextControl(bitrateLabelBounds, "Bitrate",
      IText(14.f, Colors::TextGray, "Roboto-Regular", EAlign::Near)));
    yPos += Layout::LabelHeight + 5.f;

    const IRECT bitrateSelectorBounds = IRECT(mainPanelInner.L, yPos, mainPanelInner.R, yPos + Layout::CodecSelectorHeight);
    pGraphics->AttachControl(
      new IVMenuButtonControl(bitrateSelectorBounds, kParamBitrate, "", tabStyle),
      kCtrlTagBitrateSelector
    );
    yPos += Layout::CodecSelectorHeight + 5.f;

    // Custom bitrate input (visible when "Other" is selected)
    const IRECT bitrateCustomBounds = IRECT(mainPanelInner.L, yPos, mainPanelInner.R, yPos + Layout::SliderHeight);
    pGraphics->AttachControl(
      new IVNumberBoxControl(bitrateCustomBounds, kParamBitrateCustom, nullptr, "", sliderStyle, true, 128.0, 8.0, 640.0, "%.0f kbps"),
      kCtrlTagBitrateCustom
    );
    yPos += Layout::SliderHeight + Layout::SectionSpacing;

    // Section: Sample Rate
    const IRECT sampleRateLabelBounds = IRECT(mainPanelInner.L, yPos, mainPanelInner.R, yPos + Layout::LabelHeight);
    pGraphics->AttachControl(new ITextControl(sampleRateLabelBounds, "Sample Rate",
      IText(14.f, Colors::TextGray, "Roboto-Regular", EAlign::Near)));
    yPos += Layout::LabelHeight + 5.f;

    const IRECT sampleRateSelectorBounds = IRECT(mainPanelInner.L, yPos, mainPanelInner.R, yPos + Layout::CodecSelectorHeight);
    pGraphics->AttachControl(
      new IVMenuButtonControl(sampleRateSelectorBounds, kParamSampleRate, "", tabStyle),
      kCtrlTagSampleRateSelector
    );
    yPos += Layout::CodecSelectorHeight + Layout::SectionSpacing;

    // Section: Start/Stop Button
    const IRECT startStopBounds = IRECT(mainPanelInner.L, yPos, mainPanelInner.R, yPos + Layout::CodecSelectorHeight + 5.f);
    pGraphics->AttachControl(
      new IVToggleControl(startStopBounds, kParamEnabled, "Start", tabStyle, "Stop", "Start"),
      kCtrlTagStartStopButton
    );

    //==========================================================================
    // Detail Settings Panel (Right Side) - FFmpeg Log
    //==========================================================================
    const IRECT detailPanelBounds = contentArea.GetFromRight(Layout::DetailPanelWidth);
    pGraphics->AttachControl(new IPanelControl(detailPanelBounds, Colors::Panel));

    const IRECT detailPanelInner = detailPanelBounds.GetPadded(-Layout::Padding);

    const IRECT detailTitleBounds = IRECT(detailPanelInner.L, detailPanelInner.T,
                                          detailPanelInner.R, detailPanelInner.T + Layout::LabelHeight);
    pGraphics->AttachControl(new ITextControl(detailTitleBounds, "FFmpeg Log",
      IText(16.f, Colors::TextWhite, "Roboto-Regular", EAlign::Center)));

    const IRECT logDisplayBounds = IRECT(detailPanelInner.L, detailPanelInner.T + Layout::LabelHeight + 5.f,
                                         detailPanelInner.R, detailPanelInner.B);
    pGraphics->AttachControl(
      new IMultiLineTextControl(logDisplayBounds, "Press Start to begin...",
        IText(10.f, Colors::TextGray, "Roboto-Regular", EAlign::Near, EVAlign::Top)),
      kCtrlTagLogDisplay
    );
  };
#endif

  // Log detected codecs
  for (const auto* c : availableCodecs)
    AddLogMessage("Detected: " + c->displayName + " (" + c->encoderName + ")");

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

// Helper: get effective bitrate from preset or custom input
static int GetEffectiveBitrate(iplug::Plugin* plug)
{
  int presetIdx = plug->GetParam(kParamBitrate)->Int();
  if (presetIdx >= kNumBitratePresets) // "Other"
    return plug->GetParam(kParamBitrateCustom)->Int();
  return kBitratePresets[presetIdx];
}

void CodecSim::OnReset()
{
  DebugLogCodecSim("OnReset START");

  int sampleRateIndex = GetParam(kParamSampleRate)->Int();
  if (sampleRateIndex >= 0 && sampleRateIndex < kNumSampleRatePresets)
    mSampleRate = kSampleRatePresets[sampleRateIndex];
  else
    mSampleRate = 48000;
  mNumChannels = 2;

  bool enabled = GetParam(kParamEnabled)->Bool();
  if (enabled)
  {
    std::lock_guard<std::recursive_mutex> lock(mCodecMutex);
    InitializeCodec(mCurrentCodecIndex);
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
      mCurrentCodecIndex = GetParam(kParamCodec)->Int();
      const CodecInfo* info = CodecRegistry::Instance().GetAvailableByIndex(mCurrentCodecIndex);
      if (info)
        AddLogMessage("Codec: " + info->displayName + ". Press Start to apply.");
    }
    break;
    case kParamBitrate:
    {
      int presetIdx = GetParam(kParamBitrate)->Int();
      if (presetIdx < kNumBitratePresets)
        AddLogMessage("Bitrate: " + std::to_string(kBitratePresets[presetIdx]) + " kbps. Press Start to apply.");
      else
        AddLogMessage("Bitrate: Other (custom). Press Start to apply.");
    }
    break;
    case kParamBitrateCustom:
    {
      int customBitrate = GetParam(kParamBitrateCustom)->Int();
      AddLogMessage("Custom bitrate: " + std::to_string(customBitrate) + " kbps. Press Start to apply.");
    }
    break;
    case kParamSampleRate:
    {
      int sampleRateIndex = GetParam(kParamSampleRate)->Int();
      if (sampleRateIndex >= 0 && sampleRateIndex < kNumSampleRatePresets)
        mSampleRate = kSampleRatePresets[sampleRateIndex];
      else
        mSampleRate = 48000;
      AddLogMessage("Sample rate: " + std::to_string(mSampleRate) + " Hz. Press Start to apply.");
    }
    break;
    case kParamEnabled:
    {
      bool enabled = GetParam(kParamEnabled)->Bool();
      mEnabled.store(enabled, std::memory_order_relaxed);
      DebugLogCodecSim("OnParamChange: Enabled=" + std::to_string(enabled));
      if (enabled)
      {
        mCurrentCodecIndex = GetParam(kParamCodec)->Int();
        AddLogMessage("Starting codec...");
        InitializeCodec(mCurrentCodecIndex);
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
    // Show/hide custom bitrate input based on "Other" selection
    if (IControl* pCustomBitrate = GetUI()->GetControlWithTag(kCtrlTagBitrateCustom))
    {
      bool isOther = (GetParam(kParamBitrate)->Int() >= kNumBitratePresets);
      pCustomBitrate->Hide(!isOther);
      pCustomBitrate->SetDisabled(!isOther);
    }

    if (IControl* pLogCtrl = GetUI()->GetControlWithTag(kCtrlTagLogDisplay))
    {
      std::string logText;
      {
        std::lock_guard<std::mutex> lock(mLogMutex);
        for (const auto& line : mLogMessages)
          logText += line + "\n";
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
    for (int s = 0; s < nFrames; s++)
      for (int c = 0; c < nChans; c++)
        outputs[c][s] = 0.0;
    return;
  }

  float* inBuf = mInterleavedInput.data();
  float* outBuf = mInterleavedOutput.data();

  for (int s = 0; s < nFrames; s++)
  {
    inBuf[s * 2]     = static_cast<float>(inputs[0][s]);
    inBuf[s * 2 + 1] = static_cast<float>(inputs[1][s]);
  }

  int processedSamples = mCodecProcessor->Process(inBuf, nFrames, outBuf, nFrames);

  for (int s = 0; s < nFrames; s++)
  {
    if (s < processedSamples)
    {
      outputs[0][s] = static_cast<sample>(outBuf[s * 2]);
      outputs[1][s] = static_cast<sample>(outBuf[s * 2 + 1]);
    }
    else
    {
      outputs[0][s] = 0.0;
      outputs[1][s] = 0.0;
    }
  }
}

void CodecSim::InitializeCodec(int codecIndex)
{
  std::lock_guard<std::recursive_mutex> lock(mCodecMutex);

  if (mIsInitializing) {
    DebugLogCodecSim("InitializeCodec SKIPPED (re-entrant)");
    return;
  }
  mIsInitializing = true;

  DebugLogCodecSim("InitializeCodec START: index=" + std::to_string(codecIndex));

  if (mCodecProcessor) {
    mCodecProcessor->Shutdown();
    mCodecProcessor.reset();
  }

  const CodecInfo* codecInfo = CodecRegistry::Instance().GetAvailableByIndex(codecIndex);
  if (!codecInfo)
  {
    AddLogMessage("ERROR: Invalid codec index " + std::to_string(codecIndex));
    mIsInitializing = false;
    return;
  }

  auto processor = std::make_unique<GenericCodecProcessor>(*codecInfo);

  // Connect log callback
  processor->SetLogCallback([this](const std::string& msg) {
    AddLogMessage("[ffmpeg] " + msg);
  });

  // Set bitrate from UI
  int bitrateKbps = GetEffectiveBitrate(this);
  if (!codecInfo->isLossless)
    processor->SetBitrate(bitrateKbps);

  // Initialize (launches ffmpeg processes)
  if (processor->Initialize(mSampleRate, mNumChannels))
  {
    int latency = processor->GetLatencySamples();
    SetLatency(latency);
    mLatencySamples.store(latency);

    const int maxFrames = 8192;
    mInterleavedInput.resize(maxFrames * mNumChannels);
    mInterleavedOutput.resize(maxFrames * mNumChannels);

    AddLogMessage("Started: " + codecInfo->displayName +
                 " @ " + (codecInfo->isLossless ? "lossless" : std::to_string(bitrateKbps) + "kbps") +
                 ", " + std::to_string(mSampleRate) + "Hz");
  }
  else
  {
    AddLogMessage("ERROR: Failed to start " + codecInfo->displayName);
  }

  mCodecProcessor = std::move(processor);
  mIsInitializing = false;
  DebugLogCodecSim("InitializeCodec END");
}

void CodecSim::StopCodec()
{
  std::lock_guard<std::recursive_mutex> lock(mCodecMutex);

  if (mCodecProcessor) {
    mCodecProcessor->Shutdown();
    mCodecProcessor.reset();
  }

  AddLogMessage("Codec stopped.");
}

void CodecSim::AddLogMessage(const std::string& msg)
{
  std::lock_guard<std::mutex> lock(mLogMutex);
  mLogMessages.push_back(msg);
  while (static_cast<int>(mLogMessages.size()) > kMaxLogLines)
    mLogMessages.erase(mLogMessages.begin());
}
