#include "Society/Tools/NativeModelChooserState.h"
#include "Society/Tools/NativeModelChooserProtocol.h"

#include <stdexcept>
#include <string>
#include <vector>

static void require(bool value) { if (!value) throw std::runtime_error("Model chooser contract failed"); }

int main()
{
    using society::ModelChoice;
    using society::ModelChooserState;
    const std::vector<ModelChoice> choices{
        {"/Models/Checkpoint/a.safetensors", "a.safetensors", "Checkpoint"},
        {"/Models/LLM/a.safetensors", "a.safetensors", "LLM"},
        {"/Models/VAE/decoder.safetensors", "decoder.safetensors", "VAE"},
    };
    ModelChooserState state(choices, {choices[1].path});
    require(state.selectedPaths() == std::vector<std::string>{choices[1].path});
    require(state.toggle(0));
    require(state.selectedPaths() == std::vector<std::string>{choices[0].path, choices[1].path});
    require(state.toggle(1));
    require(state.selectedPaths() == std::vector<std::string>{choices[0].path});
    require(!state.toggle(99));
    require(state.selectedPaths() == std::vector<std::string>{choices[0].path});

    bool rejected = false;
    try { ModelChooserState invalid({choices[0], choices[0]}, {}); }
    catch (const std::invalid_argument &) { rejected = true; }
    require(rejected);
    rejected = false;
    try { ModelChooserState invalid(choices, {"/different/model.safetensors"}); }
    catch (const std::invalid_argument &) { rejected = true; }
    require(rejected);
    const auto request = society::parseModelChooserRequest(
        R"({"models":[{"path":"/Models/LLM/a.safetensors","name":"a.safetensors","category":"LLM"}],"selected":["/Models/LLM/a.safetensors"]})");
    require(request.models.size() == 1 && request.selected == std::vector<std::string>{choices[1].path});
    require(society::modelChooserSelected(request.selected).find("a.safetensors") != std::string::npos);
    require(society::modelChooserFolder("/a\"b").find("a\\\"b") != std::string::npos);
    require(society::modelChooserCancel() == R"({"action":"cancel"})");
    for (const auto &bad : {R"({"models":[],"selected":["/not-offered"]})",
                            R"({"models":[],"selected":[]} garbage)",
                            R"({"models":[{"path":"a\u0000b","name":"a","category":"LLM"}],"selected":[]})"}) {
        rejected = false;
        try { (void)society::parseModelChooserRequest(bad); }
        catch (const std::invalid_argument &) { rejected = true; }
        require(rejected);
    }
}
