#pragma once

#include "Society/Content/ContentIndex.h"
#include <atomic>
#include <stdexcept>

// In-process deterministic embedding double for CLI and MCP transport tests.
// It verifies wiring and isolation, not the quality of any real model.
class ContentEmbeddingFixture final : public society::content::Embedder {
public:
    std::atomic_int embeddingCalls=0;
    QString revision(const QString &model,std::stop_token cancellation) override {
        if (cancellation.stop_requested() || model!="model://fixture") throw std::runtime_error("Fixture model unavailable.");
        return QStringLiteral("fixture-model-revision");
    }
    QList<float> embed(const QString &model,const QString &input,std::stop_token cancellation) override {
        if (cancellation.stop_requested() || model!="model://fixture") throw std::runtime_error("Fixture inference cancelled.");
        ++embeddingCalls;
        return input.contains("example") || input.contains("concept") ? QList<float>{1.0f,0.0f} : QList<float>{0.0f,1.0f};
    }
};
