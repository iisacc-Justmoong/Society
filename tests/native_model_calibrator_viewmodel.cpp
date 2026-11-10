#include "NativeModelCalibratorViewModel.h"
#include <stdexcept>
#include <string>

static void require(bool condition) {
    if (!condition) throw std::runtime_error("Model calibrator viewmodel contract failed");
}

int main() {
    NativeModelCalibratorViewModel model;
    require(!model.canAnalyze.get() && !model.canCalibrate.get());
    model.setSource("/models/input.safetensors");
    require(model.canAnalyze.get() && !model.canCalibrate.get());
    std::string analyzed;
    model.onAnalyze = [&](const auto &path) { analyzed = path; };
    model.analyze();
    require(model.busy.get() && analyzed == "/models/input.safetensors");
    model.setSource("/models/other.safetensors");
    require(model.sourcePath() == "/models/input.safetensors");
    model.analysisFinished(true, false, "Analysis complete");
    require(!model.busy.get() && !model.canCalibrate.get());
    model.setOutput("/models/input.safetensors");
    require(!model.canCalibrate.get());
    model.setOutput("/models/corrected.safetensors");
    require(model.canCalibrate.get());
    model.setRepair(true);
    bool called = false;
    model.onCalibrate = [&](const auto &input, const auto &output, bool repair) {
        called = input == "/models/input.safetensors" && output == "/models/corrected.safetensors" && repair;
    };
    model.calibrate();
    require(called && model.busy.get());
    bool cancelled = false;
    model.onCancel = [&] { cancelled = true; };
    model.cancel();
    require(cancelled && model.busy.get());
    model.calibrationFinished("Verified copy created");
    require(!model.busy.get());
    model.setSource("/models/other.safetensors");
    require(!model.canCalibrate.get());
    model.analyze();
    model.analysisFinished(false, true, "Non-finite values detected");
    model.setRepair(false);
    require(!model.canCalibrate.get());
    model.setRepair(true);
    require(model.canCalibrate.get());
}
