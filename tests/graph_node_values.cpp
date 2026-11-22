#include "Society/Space/GraphNode.h"
#include <iiGUI>
#include <algorithm>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>

namespace {
using namespace society::space;
void require(bool value, const char *message) { if (!value) throw std::runtime_error(message); }
template<class T> T take(ii::core::Result<T> result) {
    if (!result) throw std::runtime_error(result.error().message);
    return std::move(*result);
}
void good(ii::core::Result<void> result) { if (!result) throw std::runtime_error(result.error().message); }
class PaintProbe final : public ii::gui::PaintDevice {
public:
    std::size_t images = 0;
    std::vector<std::shared_ptr<const ii::gui::RasterImage>> rasters;
    std::vector<std::string> texts;
    ii::gui::PaintCapabilities capabilities() const noexcept override { return {.backdrop=true,.shadow=true,.content_transform=true,.scene_sources=true}; }
    ii::core::Result<void> begin_frame(const ii::gui::PaintFrameInfo &) override { images=0; rasters.clear(); texts.clear(); return {}; }
    ii::core::Result<void> begin_scene(const ii::gui::PaintSceneInfo &) override { return {}; }
    ii::core::Result<void> end_scene() override { return {}; }
    ii::core::Result<void> begin_layer(const ii::gui::PaintLayerInfo &) override { return {}; }
    ii::core::Result<void> draw_panels(std::span<const ii::gui::Panel>) override { return {}; }
    ii::core::Result<void> draw_text(std::span<const ii::gui::TextRun> runs) override {
        for (const auto &run : runs) texts.push_back(run.text);
        return {};
    }
    ii::core::Result<void> draw_images(std::span<const ii::gui::ImageRun> runs) override {
        images+=runs.size(); for (const auto &run:runs) rasters.push_back(run.image); return {};
    }
    ii::core::Result<void> end_layer() override { return {}; }
    ii::core::Result<void> present() override { return {}; }
    void abort_frame() noexcept override {}
};
}

int main() try {
    using namespace society::space;
    const auto image=take(ii::gui::rgba_image(1,1,{255,0,0,255}));
    const std::vector<GraphValue> values={GraphLabelValue{"Caption"}, GraphTextInputValue{"Draft"},
        GraphButtonValue{"Run"}, GraphImageValue{"asset-image","revision-1","Red pixel",image},
        GraphFileValue{"asset-file","revision-2","notes.txt"}};
    const std::vector<GraphValueFormat> formats={GraphValueFormat::Label,GraphValueFormat::TextInput,
        GraphValueFormat::Button,GraphValueFormat::Image,GraphValueFormat::File};
    std::vector<GraphNodeInput> inputs;
    for (std::size_t i=0;i<values.size();++i) {
        require(graphValueFormat(values[i])==formats[i], "each value has a distinct format");
        inputs.push_back({std::to_string(i),"Input "+std::to_string(i),"",values[i]});
    }
    GraphNode node(inputs,GraphValue{GraphTextInputValue{"Output"}},"node-values","Values","",
                   {.left=5,.right=1});
    for (std::size_t i=0;i<values.size();++i) {
        require(bool(node.setInputValue(std::to_string(i),values[i])),"all five input formats are accepted");
        require(node.inputs()[i].content==values[i],"input keeps the complete typed payload");
    }
    require(!node.setInputValue("1",GraphLabelValue{"wrong"}),"different formats cannot silently replace one another");
    require(!node.setInputValue("missing",GraphLabelValue{"missing"}),"unknown inputs fail without mutation");
    require(node.inputs()[1].content==values[1],"rejected updates preserve the previous value");

    std::vector<GraphValue> outputChanges;
    auto lease=node.observeOutput([&](const GraphValue &value){outputChanges.push_back(value);});
    require(bool(node.setOutputValue(GraphTextInputValue{"Published"})),"matching output is accepted");
    require(outputChanges.size()==1 && outputChanges.back()==GraphValue{GraphTextInputValue{"Published"}},
            "output subscribers receive the actual typed value");
    require(bool(node.setOutputValue(GraphTextInputValue{"Published"})) && outputChanges.size()==1,
            "unchanged output does not cause duplicate delivery");
    require(!node.setOutputValue(GraphLabelValue{"wrong"}) && outputChanges.size()==1,
            "incompatible output is rejected without delivery");
    require(bool(node.setInputValue("1",outputChanges.back())),"an owner can forward a typed output to an input");
    const std::vector<GraphValue> nextValues={GraphLabelValue{"Updated label"}, GraphTextInputValue{"Updated text"},
        GraphButtonValue{"Continue"}, GraphImageValue{"asset-next","revision-3","Preview",image},
        GraphFileValue{"asset-next-file","revision-4","next.txt"}};
    for (std::size_t i=0;i<values.size();++i) {
        GraphNode publisher({},values[i],"publisher-"+std::to_string(i),"Publisher","",{});
        GraphNode receiver({{"in","In","",values[i]}},GraphValue{GraphLabelValue{""}},
                           "receiver-"+std::to_string(i),"Receiver","",{});
        require(publisher.outputValue() && graphValueFormat(*publisher.outputValue())==formats[i],
                "all five formats can be published");
        require(bool(receiver.setInputValue("in",*publisher.outputValue())),"each output can be received as its matching input");
        require(receiver.inputs()[0].content==values[i],"forwarding preserves the complete value");
        bool forwarded=false;
        auto connection=publisher.observeOutput([&](const GraphValue &value){ forwarded=bool(receiver.setInputValue("in",value)); });
        require(bool(publisher.setOutputValue(nextValues[i])) && forwarded && receiver.inputs()[0].content==nextValues[i],
                "each changed output can be forwarded to a matching input by a subscriber");
    }

    std::vector<GraphValueAction> actions;
    auto source=node.viewSource({},[&](const GraphValueAction &action){actions.push_back(action);});
    auto device=std::make_shared<PaintProbe>();
    auto view=take(ii::gui::PaintView::create(device,[&](ii::gui::WidgetBuilder &builder){return source->build(builder);}));
    good(view->paint({800,900}));
    auto host=take(view->find("graph-node-inputs"));
    require(take(take(host.keyed_content_item("0","graph-input-value")).snapshot()).text=="Caption", "label is rendered as text");
    auto field=take(host.keyed_content_item("1","graph-value-input"));
    require(take(field.snapshot()).text_input &&
            take(field.text_content({0,9})).text=="Published", "text input exposes its value through its native input provider");
    require(take(take(host.keyed_content_item("2","graph-value-button")).snapshot()).text=="Run", "button renders its label");
    require(take(host.keyed_content_item("3","graph-value-image")).snapshot().has_value(), "image renders through an LVUI image");
    require(take(take(host.keyed_content_item("3","graph-value-image")).snapshot()).accessibility_name=="Red pixel",
            "image alternative text reaches accessibility");
    require(std::find(device->rasters.begin(),device->rasters.end(),image)!=device->rasters.end(),
            "the supplied image raster reaches the renderer");
    require(take(take(host.keyed_content_item("4","graph-value-file")).snapshot()).text=="notes.txt", "file renders its name");
    good(take(host.keyed_content_item("2","graph-value-button")).activate());
    good(take(host.keyed_content_item("2","graph-value-button")).activate());
    require(actions.size()==2 && actions.back().kind==GraphValueActionKind::ActivateButton,
            "repeated button activations reach the owner independently");
    good(take(host.keyed_content_item("4","graph-value-file")).activate());
    require(actions.back().kind==GraphValueActionKind::OpenFile && actions.back().slotId=="4",
            "file activation requests an action from the owner");
    good(field.focus());
    good(view->input({.kind=ii::gui::WidgetInputKind::key_down,.key=ii::gui::WidgetKey::select_all}));
    good(view->input({.kind=ii::gui::WidgetInputKind::text,.text="Edited"}));
    require(node.inputs()[1].content==GraphValue{GraphTextInputValue{"Edited"}},"text editing updates the shared input value");
    require(actions.back().kind==GraphValueActionKind::TextChanged && actions.back().slotId=="1",
            "text editing notifies the owner");
    require(bool(node.setInputValue("0",GraphLabelValue{"Updated"})),"the graph owner can update a mounted node");
    require(take(take(host.keyed_content_item("0","graph-input-value")).snapshot()).text=="Updated", "mounted view follows incoming values");
    auto outputHost=take(view->find("graph-node-outputs"));
    auto outputField=take(outputHost.keyed_content_item("output","graph-value-input"));
    require(take(outputField.text_content({0,9})).text=="Published", "typed output reaches its text field");
    good(outputField.focus());
    good(view->input({.kind=ii::gui::WidgetInputKind::key_down,.key=ii::gui::WidgetKey::select_all}));
    good(view->input({.kind=ii::gui::WidgetInputKind::text,.text="Sent"}));
    require(node.outputValue()==GraphValue{GraphTextInputValue{"Sent"}} && outputChanges.back()==GraphValue{GraphTextInputValue{"Sent"}},
            "editing an output publishes the new typed value to subscribers");
    require(actions.back().direction==GraphValueDirection::Output && actions.back().kind==GraphValueActionKind::TextChanged,
            "output interaction reports the output direction");
    good(view->close());
    require(bool(node.setInputValue("0",GraphLabelValue{"After close"})), "closed views do not retain invalid observers");

    const std::vector<const char *> outputWidgets={"graph-output-value","graph-value-input","graph-value-button","graph-value-image","graph-value-file"};
    for (std::size_t i=0;i<values.size();++i) {
        GraphNode outputNode({},values[i],"display-"+std::to_string(i),"Display","",{});
        std::vector<GraphValueAction> outputActions;
        auto outputSource=outputNode.viewSource({},[&](const GraphValueAction &event){outputActions.push_back(event);});
        auto outputView=take(ii::gui::PaintView::create(device,[&](ii::gui::WidgetBuilder &builder){return outputSource->build(builder);}));
        good(outputView->paint({640,500}));
        auto outputList=take(outputView->find("graph-node-outputs"));
        auto control=take(outputList.keyed_content_item("output",outputWidgets[i]));
        require(take(control.snapshot()).visible, "each output format renders its matching LVUI control");
        if (formats[i]==GraphValueFormat::Button || formats[i]==GraphValueFormat::File) {
            good(control.activate());
            require(outputActions.size()==1 && outputActions[0].direction==GraphValueDirection::Output &&
                    outputActions[0].slotId=="output" && outputActions[0].value==values[i],
                    "output controls send their complete typed action to the graph owner");
        }
        if (formats[i]==GraphValueFormat::Image)
            require(std::find(device->rasters.begin(),device->rasters.end(),image)!=device->rasters.end(),
                    "image output renders its actual raster");
        good(outputView->close());
    }
    GraphNode unhandled({},GraphValue{GraphButtonValue{"Run"}},"unhandled","Button","",{});
    auto unhandledSource=unhandled.viewSource();
    auto unhandledView=take(ii::gui::PaintView::create(device,[&](ii::gui::WidgetBuilder &builder){return unhandledSource->build(builder);}));
    good(unhandledView->paint({640,500}));
    auto unhandledOutput=take(unhandledView->find("graph-node-outputs"));
    require(!take(take(unhandledOutput.keyed_content_item("output","graph-value-button")).snapshot()).enabled,
            "a button without an action owner is visibly disabled");
    good(unhandledView->close());
    GraphNode emptyFile({},GraphValue{GraphFileValue{}},"empty-file","File","",{});
    std::vector<GraphValueAction> emptyFileActions;
    auto emptyFileSource=emptyFile.viewSource({},[&](const GraphValueAction &event){emptyFileActions.push_back(event);});
    auto emptyFileView=take(ii::gui::PaintView::create(device,[&](ii::gui::WidgetBuilder &builder){return emptyFileSource->build(builder);}));
    good(emptyFileView->paint({640,500}));
    auto fileOutput=take(emptyFileView->find("graph-node-outputs"));
    good(take(fileOutput.keyed_content_item("output","graph-value-file")).activate());
    require(emptyFileActions.size()==1 && emptyFileActions[0].kind==GraphValueActionKind::ChooseFile,
            "an empty file value requests selection instead of claiming a file opened");
    good(emptyFileView->close());
} catch (const std::exception &error) { std::cerr<<error.what()<<'\n'; return 1; }
