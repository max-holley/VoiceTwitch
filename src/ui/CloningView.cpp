#include <algorithm>
#include <optional>
#include <string>

#include <imgui.h>
#include <imgui_stdlib.h>

#include "app/App.h"
#include "audio/AudioPlayer.h"
#include "audio/Wav.h"
#include "cloning/CloneProject.h"
#include "cloning/RecordingScript.h"
#include "core/Paths.h"
#include "core/Platform.h"
#include "ui/Dashboard.h"

namespace vt::ui {

namespace {

constexpr int kCaptureRate = 48000;  // record at device-friendly rate, resample on save
constexpr size_t kMinRecordings = 20;
constexpr size_t kRecommendedRecordings = 150;

struct State {
  std::optional<CloneProject> project;
  std::string newName;
  std::string error;
  std::string lastSaveNote;
  size_t index = 0;
  int recordingIndex = -1;
  AudioPlayer takePlayer;
};

State& st() {
  static State s;
  return s;
}

void openProject(const std::string& name) {
  auto& s = st();
  std::string err;
  s.project = CloneProject::open(name, err);
  s.error = s.project ? "" : err;
  s.index = 0;
  s.lastSaveNote.clear();
  if (s.project) {
    // Jump to the first unrecorded prompt.
    for (size_t i = 0; i < s.project->prompts().size(); ++i) {
      if (!s.project->hasRecording(i)) {
        s.index = i;
        break;
      }
    }
  }
}

void nextUnrecorded() {
  auto& s = st();
  const size_t n = s.project->prompts().size();
  for (size_t k = 1; k <= n; ++k) {
    size_t i = (s.index + k) % n;
    if (!s.project->hasRecording(i)) {
      s.index = i;
      return;
    }
  }
}

void startRecording(App& app) {
  auto& s = st();
  s.takePlayer.stop();
  s.takePlayer.end();
  std::string err;
  if (!app.recorder().start(kCaptureRate, err)) {
    s.error = err;
    return;
  }
  s.recordingIndex = static_cast<int>(s.index);
  s.error.clear();
}

void stopRecording(App& app, bool keep) {
  auto& s = st();
  auto samples = app.recorder().stop();
  int idx = s.recordingIndex;
  s.recordingIndex = -1;
  if (!keep || idx < 0 || !s.project) return;
  std::string err;
  dsp::ClipCheck check;
  if (!s.project->saveRecording(static_cast<size_t>(idx), std::move(samples), kCaptureRate, err, &check)) {
    s.error = err;
    return;
  }
  s.lastSaveNote.clear();
  if (check.clipped) s.lastSaveNote += "Clipping detected - move back from the mic or lower input gain. ";
  if (check.tooQuiet) s.lastSaveNote += "Very quiet take - raise input gain. ";
  if (check.tooShort) s.lastSaveNote += "Very short take - did the whole line get recorded? ";
  if (s.lastSaveNote.empty()) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "Saved (%.1fs).", check.durationSec);
    s.lastSaveNote = buf;
    nextUnrecorded();
  }
}

void playTake(size_t index) {
  auto& s = st();
  std::vector<float> samples;
  std::string err;
  if (!wav::readMono(s.project->wavPath(index), CloneProject::kSampleRate, samples, err)) {
    s.error = err;
    return;
  }
  s.takePlayer.stop();
  if (s.takePlayer.begin(CloneProject::kSampleRate, err)) {
    s.takePlayer.push(samples);
  } else {
    s.error = err;
  }
}

void projectPicker(App& app) {
  auto& s = st();
  auto projects = CloneProject::listProjects();
  ImGui::SetNextItemWidth(ImGui::GetFontSize() * 14);
  const char* current = s.project ? s.project->name().c_str() : "Select a project...";
  if (ImGui::BeginCombo("Project", current)) {
    for (const auto& p : projects) {
      if (ImGui::Selectable(p.c_str(), s.project && s.project->name() == p)) openProject(p);
    }
    ImGui::EndCombo();
  }
  ImGui::SameLine();
  ImGui::SetNextItemWidth(ImGui::GetFontSize() * 12);
  ImGui::InputTextWithHint("##newname", "new-voice-name", &s.newName);
  ImGui::SameLine();
  if (ImGui::Button("Create")) {
    std::string err;
    auto prompts = loadRecordingScript(paths::assetsDir() / "recording_script.txt");
    auto p = CloneProject::create(s.newName, prompts, err);
    if (p) {
      openProject(s.newName);
      s.newName.clear();
    } else {
      s.error = err;
    }
  }
  (void)app;
}

void recordingPanel(App& app) {
  auto& s = st();
  auto& proj = *s.project;
  const size_t total = proj.prompts().size();
  const size_t done = proj.recordedCount();
  const double secs = proj.recordedSeconds();

  sectionHeader("1. Record your voice");
  char overlay[96];
  std::snprintf(overlay, sizeof(overlay), "%zu / %zu prompts  (%d:%02d of audio)", done, total,
                static_cast<int>(secs) / 60, static_cast<int>(secs) % 60);
  ImGui::ProgressBar(total ? static_cast<float>(done) / static_cast<float>(total) : 0.f, ImVec2(-1, 0), overlay);
  ImGui::TextDisabled("Quiet room, consistent mic distance, natural delivery. At least %zu takes to train; "
                      "%zu+ (about 15 minutes) gives noticeably better results.",
                      kMinRecordings, kRecommendedRecordings);

  ImGui::BeginChild("prompt", ImVec2(0, ImGui::GetFontSize() * 6.5f), ImGuiChildFlags_Borders);
  ImGui::TextDisabled("Prompt %zu of %zu%s", s.index + 1, total, proj.hasRecording(s.index) ? "  (recorded)" : "");
  ImGui::SetWindowFontScale(1.45f);
  ImGui::TextWrapped("%s", proj.prompts()[s.index].c_str());
  ImGui::SetWindowFontScale(1.0f);
  ImGui::EndChild();

  const bool recording = app.recorder().isRecording();
  ImGui::BeginDisabled(recording);
  if (ImGui::Button("< Prev") && s.index > 0) --s.index;
  ImGui::SameLine();
  if (ImGui::Button("Next >") && s.index + 1 < total) ++s.index;
  ImGui::SameLine();
  if (ImGui::Button("Next unrecorded")) nextUnrecorded();
  ImGui::EndDisabled();
  ImGui::SameLine(0, 30);

  if (!recording) {
    if (ImGui::Button(proj.hasRecording(s.index) ? "Re-record (Space)" : "Record (Space)")) startRecording(app);
  } else {
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.75f, 0.15f, 0.15f, 1.0f));
    if (ImGui::Button("Stop & save (Space)")) stopRecording(app, true);
    ImGui::PopStyleColor();
    ImGui::SameLine();
    if (ImGui::Button("Discard (Esc)")) stopRecording(app, false);
  }
  if (!recording && proj.hasRecording(s.index)) {
    ImGui::SameLine();
    if (ImGui::Button("Play take")) playTake(s.index);
    ImGui::SameLine();
    if (ImGui::Button("Delete take")) proj.deleteRecording(s.index);
  }

  // Keyboard shortcuts while no text field has focus.
  if (!ImGui::GetIO().WantTextInput) {
    if (ImGui::IsKeyPressed(ImGuiKey_Space, false)) {
      if (recording) stopRecording(app, true);
      else startRecording(app);
    } else if (recording && ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
      stopRecording(app, false);
    } else if (!recording && ImGui::IsKeyPressed(ImGuiKey_RightArrow) && s.index + 1 < total) {
      ++s.index;
    } else if (!recording && ImGui::IsKeyPressed(ImGuiKey_LeftArrow) && s.index > 0) {
      --s.index;
    }
  }

  float level = recording ? app.recorder().inputLevel() : 0.0f;
  char lvl[48];
  std::snprintf(lvl, sizeof(lvl), recording ? "Recording %.1fs" : "Mic idle", app.recorder().secondsRecorded());
  ImGui::PushStyleColor(ImGuiCol_PlotHistogram,
                        level > 0.98f ? ImVec4(0.9f, 0.3f, 0.3f, 1) : ImVec4(0.3f, 0.8f, 0.45f, 1));
  ImGui::ProgressBar(level, ImVec2(-1, ImGui::GetFontSize() * 0.9f), lvl);
  ImGui::PopStyleColor();

  if (!s.lastSaveNote.empty()) ImGui::TextDisabled("%s", s.lastSaveNote.c_str());
  if (ImGui::Button("Open project folder")) platform::openFolder(proj.dir());
}

void trainingPanel(App& app) {
  auto& s = st();
  auto& proj = *s.project;
  auto& tr = app.training();
  auto status = tr.status();
  const bool running = tr.running();
  const size_t done = proj.recordedCount();
  auto& cfg = app.config();

  sectionHeader("2. Train the voice");
  ImGui::TextWrapped(
      "Training fine-tunes a Piper base voice on your recordings using Piper's Python training tools. It runs "
      "outside the app, needs Python 3.9+ and several GB of disk, and takes hours - an NVIDIA GPU is strongly "
      "recommended. It is a one-off task; streaming never depends on it.");
  ImGui::TextDisabled("Training environment: %s",
                      TrainingRunner::environmentReady() ? "installed" : "will be installed on first run");
  ImGui::TextDisabled("%d extra epochs | batch %d | espeak '%s' | base checkpoint and Python path in Settings",
                      cfg.trainingEpochs, cfg.trainingBatchSize, cfg.cloneEspeakVoice.c_str());

  ImGui::BeginDisabled(running || done < kMinRecordings || app.recorder().isRecording());
  if (ImGui::Button("Start training")) {
    std::string err;
    if (proj.writeMetadata(err) == 0) {
      s.error = err.empty() ? "No recordings" : err;
    } else {
      TrainingRequest req;
      req.projectName = proj.name();
      req.projectDir = proj.dir();
      req.outputModel = proj.modelPath();
      req.pythonPath = cfg.pythonPath;
      req.espeakVoice = cfg.cloneEspeakVoice;
      req.baseCheckpointUrl = cfg.baseCheckpointUrl;
      req.epochs = cfg.trainingEpochs;
      req.batchSize = cfg.trainingBatchSize;
      req.sampleRate = CloneProject::kSampleRate;
      if (!tr.start(req, err)) s.error = err;
    }
  }
  ImGui::EndDisabled();
  if (done < kMinRecordings) {
    ImGui::SameLine();
    ImGui::TextDisabled("Record at least %zu prompts first.", kMinRecordings);
  }
  if (running) {
    ImGui::SameLine();
    if (ImGui::Button("Cancel training")) tr.cancel();
  }

  if (status.stage != TrainingRunner::Stage::Idle && status.project == proj.name()) {
    ImGui::Text("%s", TrainingRunner::stageName(status.stage));
    if (!status.message.empty()) {
      ImGui::SameLine();
      ImGui::TextDisabled("- %s", status.message.c_str());
    }
    if (status.stage == TrainingRunner::Stage::Training && status.targetEpoch > 0) {
      char ov[64];
      std::snprintf(ov, sizeof(ov), "epoch %d / %d", status.epoch, status.targetEpoch);
      ImGui::ProgressBar(status.progress >= 0 ? status.progress : 0.0f, ImVec2(-1, 0), ov);
    } else if (running) {
      ImGui::ProgressBar(-1.0f * static_cast<float>(ImGui::GetTime()), ImVec2(-1, 0), "working...");
    }
    if (ImGui::TreeNode("Training output")) {
      auto tail = tr.outputTail();
      ImGui::BeginChild("trainlog", ImVec2(0, ImGui::GetFontSize() * 14), ImGuiChildFlags_Borders,
                        ImGuiWindowFlags_HorizontalScrollbar);
      for (const auto& l : tail) ImGui::TextUnformatted(l.c_str());
      if (running) ImGui::SetScrollHereY(1.0f);
      ImGui::EndChild();
      ImGui::TreePop();
    }
  }

  if (proj.hasModel()) {
    sectionHeader("3. Use it");
    auto id = ModelManager::voiceIdFor(proj.modelPath());
    bool active = cfg.activeVoice == id;
    ImGui::BeginDisabled(active);
    if (ImGui::Button(active ? "Active voice" : "Use this voice")) {
      app.models().refreshInstalled();
      app.setActiveVoice(id);
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Preview")) app.pipeline().preview("Hey chat, this is my own voice now. Pretty cool, right?", proj.modelPath());
  }
}

}  // namespace

void drawCloningView(App& app) {
  auto& s = st();
  ImGui::Text("Voice Cloning");
  ImGui::Separator();
  ImGui::BeginChild("clone_scroll");
  projectPicker(app);
  if (!s.error.empty()) ImGui::TextColored(ImVec4(0.9f, 0.3f, 0.3f, 1), "%s", s.error.c_str());

  if (!s.project) {
    ImGui::Spacing();
    ImGui::TextWrapped(
        "Create a project to record your own voice. You'll read a scripted list of short sentences that covers "
        "the sounds of English; the recordings are cleaned up (silence trimmed, normalized, resampled to "
        "22.05 kHz) and then used to fine-tune a Piper voice.");
  } else {
    recordingPanel(app);
    trainingPanel(app);
  }
  ImGui::EndChild();
}

}  // namespace vt::ui
