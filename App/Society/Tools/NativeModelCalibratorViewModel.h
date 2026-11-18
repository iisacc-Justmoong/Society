#pragma once

#include <iiSDKCore>
#include <functional>
#include <string>

struct NativeModelCalibratorViewModel final {
    ii::core::Observable<std::string> sourceLabel{"Choose a model…"};
    ii::core::Observable<std::string> outputLabel{"Choose destination…"};
    ii::core::Observable<std::string> status{"Choose a model to begin"};
    ii::core::Observable<bool> busy{false}, canAnalyze{false}, canCalibrate{false}, repairNonfinite{false};
    std::function<void()> onChooseSource, onChooseOutput, onCancel;
    std::function<void(const std::string &)> onAnalyze;
    std::function<void(const std::string &, const std::string &, bool)> onCalibrate;

    [[nodiscard]] const std::string &sourcePath() const { return source_; }
    void setSource(std::string path);
    void setOutput(std::string path);
    void setRepair(bool value);
    void chooseSource();
    void chooseOutput();
    void analyze();
    void calibrate();
    void cancel();
    void analysisFinished(bool packageVerified, bool numericRepairAvailable, std::string message);
    void calibrationFinished(std::string message);
    void failed(std::string message);

private:
    void update();
    std::string source_, output_;
    bool verified_ = false, numericRepairAvailable_ = false;
};
