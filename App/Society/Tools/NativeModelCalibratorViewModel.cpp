#include "NativeModelCalibratorViewModel.h"

#include <filesystem>
#include <utility>

void NativeModelCalibratorViewModel::update() {
    canAnalyze.set(!busy.get() && !source_.empty());
    canCalibrate.set(!busy.get() && (verified_ || (numericRepairAvailable_ && repairNonfinite.get())) && !output_.empty()
        && std::filesystem::path(source_).lexically_normal() != std::filesystem::path(output_).lexically_normal());
}
void NativeModelCalibratorViewModel::setSource(std::string path) {
    if (busy.get() || path.empty()) return;
    source_ = std::move(path); sourceLabel.set(source_); verified_ = false; numericRepairAvailable_ = false;
    status.set("Model selected. Analyze before creating a corrected copy."); update();
}
void NativeModelCalibratorViewModel::setOutput(std::string path) {
    if (busy.get() || path.empty()) return;
    output_ = std::move(path); outputLabel.set(output_); update();
}
void NativeModelCalibratorViewModel::setRepair(bool value) {
    if (!busy.get()) { repairNonfinite.set(value); update(); }
}
void NativeModelCalibratorViewModel::chooseSource() { if (!busy.get() && onChooseSource) onChooseSource(); }
void NativeModelCalibratorViewModel::chooseOutput() { if (!busy.get() && onChooseOutput) onChooseOutput(); }
void NativeModelCalibratorViewModel::analyze() {
    if (!canAnalyze.get() || !onAnalyze) return;
    verified_ = false; busy.set(true); status.set("Analyzing model…"); update(); onAnalyze(source_);
}
void NativeModelCalibratorViewModel::calibrate() {
    if (!canCalibrate.get() || !onCalibrate) return;
    busy.set(true); status.set("Creating a verified corrected copy…"); update();
    onCalibrate(source_, output_, repairNonfinite.get());
}
void NativeModelCalibratorViewModel::cancel() { if (busy.get() && onCancel) onCancel(); }
void NativeModelCalibratorViewModel::analysisFinished(bool verified, bool numericAvailable, std::string message) {
    verified_ = verified; numericRepairAvailable_ = numericAvailable;
    busy.set(false); status.set(std::move(message)); update();
}
void NativeModelCalibratorViewModel::calibrationFinished(std::string message) {
    busy.set(false); status.set(std::move(message)); update();
}
void NativeModelCalibratorViewModel::failed(std::string message) {
    verified_ = false; numericRepairAvailable_ = false; busy.set(false); status.set(std::move(message)); update();
}
