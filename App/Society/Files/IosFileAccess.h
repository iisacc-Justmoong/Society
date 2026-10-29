#pragma once

class QWindow;
class QString;
class ModelImporter;
bool presentIosModelPicker(QWindow *window, ModelImporter *importer);
bool previewIosFile(const QString &path);
void observeIosImportLifecycle(ModelImporter *importer);
