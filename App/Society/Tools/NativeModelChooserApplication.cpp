#include "NativeModelChooser.generated.h"
#include "NativeModelChooserProtocol.h"

#include <iiGUI/Application.h>
#include <array>
#include <iostream>
#include <memory>
#include <stdexcept>

namespace {
using View = lvui_generated::NativeModelChooser;
struct ChooserApplication final {
    society::ModelChooserRequest request;
    std::shared_ptr<ii::gui::Host> host;
    std::shared_ptr<View> view = std::make_shared<View>();
    NativeModelChooserViewModel *model = nullptr;
    bool replied = false;
};

std::string readRequest()
{
    constexpr std::size_t limit = 16 * 1024 * 1024;
    std::string input;
    std::array<char, 8192> buffer{};
    while (std::cin) {
        std::cin.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const auto count = static_cast<std::size_t>(std::cin.gcount());
        if (input.size() + count > limit) throw std::invalid_argument("Chooser request too large");
        input.append(buffer.data(), count);
    }
    return input;
}
}

ii::core::Result<void> ii::runtime::configure_application(Application &app)
{
    auto state = std::make_shared<ChooserApplication>();
    try { state->request = society::parseModelChooserRequest(readRequest()); }
    catch (const std::exception &error) { return core::fail(core::ErrorCode::invalid_argument, error.what()); }
    auto reply = [state, &app](std::string result) {
        if (state->replied) return;
        state->replied = true;
        std::cout << result << std::flush;
        (void)app.quit();
    };
    auto host = gui::attach_application(app, View::options(), {
        .resized = [state](gui::Size size) { if (state->model) state->model->resize(size.width, size.height); },
        .close_requested = [state] {
            if (!state->replied) { state->replied = true; std::cout << society::modelChooserCancel() << std::flush; }
            return true;
        },
        .closed = [state] { state->model = nullptr; }
    });
    if (!host) return std::unexpected(host.error());
    state->host = *host;
    return app.attach(gui::view_attachment("model-chooser", state->host, state->view,
        std::function<void(View::Scope &)>{[state, reply](View::Scope &scope) {
            auto &model = scope.backend_0;
            state->model = &model;
            model.initialize(std::move(state->request.models), state->request.selected);
            const auto snapshot = state->host->snapshot();
            if (snapshot) model.resize(snapshot->size.width, snapshot->size.height);
            model.onConfirm = [reply](const auto &paths) { reply(society::modelChooserSelected(paths)); };
            model.onCancel = [reply] { reply(society::modelChooserCancel()); };
            model.onBrowse = [state, reply] {
                auto result = state->host->choose_file(
                    {.mode = gui::FileDialogMode::directory, .title = "Choose a model folder"},
                    [reply](core::Result<gui::FileDialogSelection> selection) {
                        if (selection && !selection->canceled && selection->paths.size() == 1)
                            reply(society::modelChooserFolder(selection->paths.front()));
                    });
                if (!result) std::cerr << result.error().message << '\n';
            };
        }}));
}
