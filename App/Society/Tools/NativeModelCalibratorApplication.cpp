#include "NativeModelCalibrator.generated.h"
#include "ModelPackaging/ModelInspection.hpp"
#include "ModelPackaging/ModelPackaging.hpp"

#include <iiGUI/Application.h>
#include <json-c/json.h>
#include <chrono>
#include <filesystem>
#include <memory>
#include <stop_token>
#include <string>

namespace {
using View = lvui_generated::NativeModelCalibrator;
struct State final {
    std::shared_ptr<ii::gui::Host> host;
    std::shared_ptr<View> view = std::make_shared<View>();
    NativeModelCalibratorViewModel *model = nullptr;
    std::shared_ptr<std::stop_source> stop;
    std::string directory;
    bool packageVerified = false;
};

std::int64_t number(const std::string &text, const char *key) {
    std::unique_ptr<json_object, decltype(&json_object_put)> root(json_tokener_parse(text.c_str()), json_object_put);
    json_object *value = nullptr;
    if (!root || !json_object_object_get_ex(root.get(), key, &value) || !value)
        throw std::runtime_error("Model analysis returned an incomplete report");
    return json_object_get_int64(value);
}

auto progressObserver(std::weak_ptr<State> weak, ii::gui::UiExecutor ui) {
    auto last = std::make_shared<std::chrono::steady_clock::time_point>();
    return [weak, ui, last](const iild::ModelPackagingProgress &p) mutable {
        const auto now = std::chrono::steady_clock::now();
        if (now - *last < std::chrono::milliseconds(250)) return;
        *last = now;
        const auto message = p.phase + " · " + p.detail;
        (void)ui.post([weak, message] {
            if (auto state = weak.lock(); state && state->model) state->model->status.set(message);
        });
    };
}
}

ii::core::Result<void> ii::runtime::configure_application(Application &app) {
    auto state = std::make_shared<State>();
    const auto args = app.arguments();
    if (args.size() > 1) state->directory = args.back();
    const std::weak_ptr<State> weak = state;
    auto host = gui::attach_application(app, View::options(), {
        .close_requested = [weak] {
            if (auto s = weak.lock(); s && s->stop) s->stop->request_stop();
            return true;
        },
        .closed = [weak] { if (auto s = weak.lock()) s->model = nullptr; }
    });
    if (!host) return std::unexpected(host.error());
    state->host = *host;
    return app.attach(gui::view_attachment("model-calibrator", state->host, state->view,
        std::function<void(View::Scope &)>{[state, weak, &app](View::Scope &scope) {
            auto &model = scope.backend_0;
            state->model = &model;
            model.onChooseSource = [weak] {
                auto s = weak.lock(); if (!s) return;
                auto result = s->host->choose_file({.mode=gui::FileDialogMode::open,
                    .title="Choose a model", .initial_directory=s->directory,
                    .filters={{"Safetensors", {"safetensors"}}}},
                    [weak](core::Result<gui::FileDialogSelection> selection) {
                        if (auto s = weak.lock(); s && s->model && selection && !selection->canceled && selection->paths.size()==1) {
                            s->packageVerified=false; s->model->setSource(selection->paths.front());
                        }
                    });
                if (!result && s->model) s->model->failed(result.error().message);
            };
            model.onChooseOutput = [weak] {
                auto s = weak.lock(); if (!s) return;
                auto result = s->host->choose_file({.mode=gui::FileDialogMode::save,
                    .title="Save corrected model", .filename="corrected.safetensors", .initial_directory=s->directory,
                    .filters={{"Safetensors", {"safetensors"}}}},
                    [weak](core::Result<gui::FileDialogSelection> selection) {
                        if (auto s = weak.lock(); s && s->model && selection && !selection->canceled && selection->paths.size()==1)
                            s->model->setOutput(selection->paths.front());
                    });
                if (!result && s->model) s->model->failed(result.error().message);
            };
            model.onCancel = [weak] { if (auto s = weak.lock(); s && s->stop) s->stop->request_stop(); };
            model.onAnalyze = [weak, &app](const std::string &path) {
                auto s = weak.lock(); if (!s || !s->model) return;
                s->stop=std::make_shared<std::stop_source>();
                const auto stop=s->stop; const auto ui=s->host->executor();
                auto posted=app.post([weak, ui, stop, path] {
                    try {
                        const auto observer=progressObserver(weak,ui);
                        const auto inspection=iild::inspectModel(path);
                        const auto health=iild::inspectModelWeights(path,stop->get_token(),observer);
                        const auto bad=number(health,"nonfinite_values");
                        const auto skipped=number(health,"unchecked_tensors");
                        bool package=false; std::string packageError;
                        try { (void)iild::verifyModelPackage(path,stop->get_token(),observer); package=true; }
                        catch (const std::exception &error) { if (stop->stop_requested()) throw; packageError=error.what(); }
                        const std::string family=inspection.recognized ? inspection.family : "Architecture unavailable";
                        const auto message=family+" · "+std::to_string(bad)+" non-finite values · "
                            +std::to_string(skipped)+" unchecked tensors. "
                            +(package ? "Package verified." : "Package correction unavailable: "+packageError);
                        (void)ui.post([weak,package,bad,message] {
                            if (auto s=weak.lock();s && s->model) {
                                s->packageVerified=package; s->model->analysisFinished(package,bad>0,message);
                            }
                        });
                    } catch (const std::exception &error) {
                        const std::string message=stop->stop_requested()?"Analysis canceled":error.what();
                        (void)ui.post([weak,message] { if(auto s=weak.lock();s && s->model) s->model->failed(message); });
                    }
                });
                if (!posted) s->model->failed("Could not start model analysis");
            };
            model.onCalibrate = [weak, &app](const std::string &input,const std::string &output,bool repair) {
                auto s=weak.lock(); if (!s || !s->model) return;
                s->stop=std::make_shared<std::stop_source>();
                const auto stop=s->stop; const auto ui=s->host->executor(); const bool package=s->packageVerified;
                auto posted=app.post([weak,ui,stop,input,output,repair,package] {
                    try {
                        const auto observer=progressObserver(weak,ui);
                        const auto report=package
                            ? iild::calibrateModelPackage(input,output,stop->get_token(),observer,{.replaceNonfiniteWithZero=repair})
                            : iild::calibrateModelFile(input,output,stop->get_token(),observer);
                        const auto changed=number(report,"nonfinite_values_replaced");
                        const auto message="Verified copy created · "+std::to_string(changed)+" non-finite values replaced";
                        (void)ui.post([weak,message] { if(auto s=weak.lock();s && s->model) s->model->calibrationFinished(message); });
                    } catch (const std::exception &error) {
                        const std::string message=stop->stop_requested()?"Calibration canceled":error.what();
                        (void)ui.post([weak,message] { if(auto s=weak.lock();s && s->model) s->model->failed(message); });
                    }
                });
                if (!posted) s->model->failed("Could not start model calibration");
            };
        }}));
}
