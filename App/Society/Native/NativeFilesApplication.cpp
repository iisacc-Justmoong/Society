#include "NativeFiles.generated.h"
#include "NativeFilesClient.h"
#include "NativeFileOpen.h"
#include <iiGUI/Application.h>
#include <iiGUI/Desktop.h>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>

namespace {
using View=lvui_generated::NativeFiles;
struct NativeFilesSession final {
    society::native::NativeBackendConnection connection;
    society::native::NativeFilesClient binding;
    NativeFilesSession(ii::runtime::EventLoop &loop,NativeFilesViewModel &model) : connection(loop),binding(connection,model) {}
};
struct AppState final {
    std::shared_ptr<ii::gui::Host> host;
    std::shared_ptr<View> view=std::make_shared<View>();
    NativeFilesViewModel *model=nullptr;
};
std::string socketPath() {
    if (const auto *root=std::getenv("SOCIETY_LOCAL_BROKER_DIR"); root && *root)
        return (std::filesystem::path(root)/"backend.sock").string();
    if (const auto *home=std::getenv("HOME"); home && *home)
        return (std::filesystem::path(home)/".iisacc-society-ipc-v1"/"backend.sock").string();
    return {};
}
}
ii::core::Result<void> ii::runtime::configure_application(Application &app) {
    auto state=std::make_shared<AppState>();
    std::weak_ptr<AppState> weak=state;
    auto host=gui::attach_application(app,View::options(),{
        .resized=[weak](gui::Size size){if(auto state=weak.lock();state && state->model)state->model->resize(size.width,size.height);},
        .closed=[weak]{if(auto state=weak.lock())state->model=nullptr;}
    });
    if (!host) return std::unexpected(host.error());
    state->host=*host;
    return app.attach(gui::view_attachment("native-files-migration",state->host,state->view,
        std::function<void(View::Scope &)>{[state,&app](View::Scope &scope){
            auto &model=scope.backend_0; state->model=&model;
            model.onOpenFile=[](const std::string &path) { return society::native::openNativeFile(path); };
            const std::weak_ptr<AppState> weakState=state;
            model.onChooseFiles=[weakState] {
                const auto owner=weakState.lock(); if (!owner || !owner->model) return;
                ii::gui::FileDialogOptions options;
                options.mode=ii::gui::FileDialogMode::open; options.title="Import files"; options.multiple=true;
                const auto started=owner->host->choose_file(std::move(options),[weakState](ii::core::Result<ii::gui::FileDialogSelection> result) {
                    const auto owner=weakState.lock(); if (!owner || !owner->model) return;
                    if (!result) { owner->model->fileChooserFailed(result.error().message); return; }
                    owner->model->filesChosen(result->canceled ? std::vector<std::string>{} : result->paths);
                });
                if (!started && owner->model) owner->model->fileChooserFailed(started.error().message);
            };
            model.onChooseModels=[weakState] {
                const auto owner=weakState.lock(); if (!owner || !owner->model) return;
                ii::gui::FileDialogOptions options;
                options.mode=ii::gui::FileDialogMode::open; options.title="Import models";
                options.multiple=true; options.filters={{"Safetensors",{"safetensors","safetensor"}}};
                const auto started=owner->host->choose_file(std::move(options),[weakState](ii::core::Result<ii::gui::FileDialogSelection> result) {
                    const auto owner=weakState.lock(); if (!owner || !owner->model) return;
                    if (!result) { owner->model->modelChooserFailed(result.error().message); return; }
                    owner->model->modelsChosen(result->canceled ? std::vector<std::string>{} : result->paths);
                });
                if (!started && owner->model) owner->model->modelChooserFailed(started.error().message);
            };
            if (auto snapshot=state->host->snapshot()) model.resize(snapshot->size.width,snapshot->size.height);
            auto session=std::make_shared<NativeFilesSession>(app.event_loop(),model);
            model.retainServices(session);
            const auto address=socketPath();
            std::weak_ptr<NativeFilesSession> weakSession=session;
            model.onReconnect=[weakSession,&model,address] {
                const auto session=weakSession.lock(); if (!session) return;
                session->connection.close();
                if (address.empty()) { model.backendError("Cannot locate the Society backend"); return; }
                if (auto result=session->connection.connect(address); !result) model.backendError(result.error());
            };
            model.onReconnect();
        }}));
}
