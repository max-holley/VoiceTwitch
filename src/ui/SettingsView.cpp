#include <string>

#include <imgui.h>
#include <imgui_stdlib.h>

#include "app/App.h"
#include "core/Paths.h"
#include "core/Platform.h"
#include "ui/Dashboard.h"

namespace vt::ui {

void drawSettingsView(App& app) {
  auto& cfg = app.config();
  bool changed = false;

  pageTitle("Settings", "Reading behaviour, voice output and cloning options");
  ImGui::BeginChild("settings_scroll");

  sectionHeader("Reading");
  beginCard("card_reading");
  const float sw = ImGui::GetFontSize() * 24;
  bool reading = cfg.readingEnabled;
  if (toggle("Read chat aloud", &reading)) {
    cfg.readingEnabled = reading;
    changed = true;
  }
  helpMarker("Start/stop reading. While stopped, incoming messages are not queued.");
  changed |= toggle("Announce username (\"name says ...\")", &cfg.announceUsername);
  changed |= toggle("Ignore chat commands (messages starting with !)", &cfg.skipCommands);
  ImGui::Spacing();
  changed |= sliderInt("Max characters per message", &cfg.maxMessageChars, 50, 500, "%d", sw);
  endCard();

  sectionHeader("Voice output");
  beginCard("card_voice");
  float volPct = cfg.volume * 100.0f;
  if (sliderFloat("Volume", &volPct, 0.0f, 100.0f, "%.0f%%", sw)) {
    cfg.volume = volPct / 100.0f;
    changed = true;
  }
  changed |= sliderFloat("Speaking rate", &cfg.rate, 0.5f, 2.0f, "%.2fx", sw);
  changed |= sliderFloat("Pause between sentences", &cfg.sentenceSilence, 0.0f, 1.0f, "%.2f s", sw);
  ImGui::BeginDisabled();
  float pitch = 1.0f;
  sliderFloat("Pitch (not supported)", &pitch, 0.5f, 2.0f, "n/a", sw);
  ImGui::EndDisabled();
  helpMarker("Piper (VITS) voices don't support independent pitch control. Pitch is a property of the voice "
             "model - try a different voice, or clone your own.");
  ImGui::Spacing();
  if (primaryButton("Test voice")) {
    app.applySettings();
    app.pipeline().preview("This is a test of the current voice settings.", {});
  }
  endCard();

  sectionHeader("Voice cloning");
  beginCard("card_cloning");
  ImGui::SetNextItemWidth(ImGui::GetFontSize() * 22);
  changed |= ImGui::InputTextWithHint("Python executable", "auto-detect (py -3 / python3)", &cfg.pythonPath);
  helpMarker("Python 3.9+ used once to create the training environment. Leave empty to auto-detect.");
  ImGui::SetNextItemWidth(ImGui::GetFontSize() * 8);
  changed |= ImGui::InputText("espeak-ng voice", &cfg.cloneEspeakVoice);
  helpMarker("Language of your recordings, e.g. en-us, en-gb, de, fr-fr.");
  ImGui::SetNextItemWidth(ImGui::GetFontSize() * 8);
  changed |= ImGui::InputInt("Extra training epochs", &cfg.trainingEpochs, 50, 100);
  ImGui::SetNextItemWidth(ImGui::GetFontSize() * 8);
  changed |= ImGui::InputInt("Batch size", &cfg.trainingBatchSize, 1, 4);
  helpMarker("Lower this if training runs out of GPU memory.");
  ImGui::SetNextItemWidth(-1);
  changed |= ImGui::InputText("##ckpt", &cfg.baseCheckpointUrl);
  ImGui::TextDisabled("Base checkpoint URL (a medium-quality Piper checkpoint from huggingface.co/datasets/rhasspy/piper-checkpoints)");
  if (cfg.trainingEpochs < 1) cfg.trainingEpochs = 1;
  if (cfg.trainingBatchSize < 1) cfg.trainingBatchSize = 1;
  endCard();

  sectionHeader("Storage");
  beginCard("card_storage");
  ImGui::TextDisabled("Data folder: %s", paths::toUtf8(paths::dataDir()).c_str());
  if (button("Open data folder")) platform::openFolder(paths::dataDir());
  endCard();

  sectionHeader("About");
  beginCard("card_about");
  ImGui::TextWrapped("VoiceTwitch %s - offline TTS for Twitch chat. Speech synthesis by Piper voices on "
                     "onnxruntime; phonemization by espeak-ng (GPL-3.0). Voice models are downloaded from the Piper "
                     "project and carry their own licenses (see each model card).",
                     VT_VERSION);
  endCard();

  ImGui::EndChild();

  if (changed) {
    app.markConfigDirty();
    app.applySettings();
  }
}

}  // namespace vt::ui
