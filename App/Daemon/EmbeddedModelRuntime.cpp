#include "EmbeddedModelRuntime.h"

#include <ObjectSource.h>
#include <iiFileProviderNativeDevice.h>
#include <QByteArrayView>
#include <QCryptographicHash>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <algorithm>
#include <filesystem>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {
namespace fs=std::filesystem;
using namespace iiLocalLLM;

/// Convert a Qt path without depending on the current locale.
fs::path native(const QString &path) { return fs::path(path.toStdU16String()); }

/// Restrict an existing asset to a direct, canonical descendant of Models.
void verifyPath(const fs::path &root,const fs::path &candidate) {
    std::error_code error;
    if (!root.is_absolute() || !candidate.is_absolute() || root.lexically_normal()!=root ||
        candidate.lexically_normal()!=candidate || fs::canonical(root,error)!=root || error)
        throw Error(ErrorCode::IntegrityFailure,"The Society Models root is unavailable or redirected");
    error.clear(); const auto status=fs::symlink_status(candidate,error);
    if (error || fs::is_symlink(status) || !fs::is_regular_file(status) || fs::canonical(candidate,error)!=candidate || error)
        throw Error(ErrorCode::IntegrityFailure,"The Society model file is unavailable or redirected");
    const auto relative=candidate.lexically_relative(root);
    if (relative.empty() || relative.is_absolute() || *relative.begin()=="..")
        throw Error(ErrorCode::IntegrityFailure,"The model file is outside Society Models");
}

/// Revalidate the source against the selected component's size and checksum.
/// Reads are bounded, cancellable and never create an extracted weight file.
iiFileProvider::ObjectSourceIdentity verifySource(const fs::path &root,const EmbeddedModelSource &source,
                                                   const CancellationToken &cancel) {
    const auto path=native(source.path); verifyPath(root,path);
    const auto before=iiFileProvider::ObjectSource::inspect(path);
    if (!source.size || source.offset>before.size || source.size>before.size-source.offset ||
        source.offset>quint64(std::numeric_limits<std::int64_t>::max()) ||
        source.size>quint64(std::numeric_limits<std::int64_t>::max())-source.offset ||
        !QRegularExpression(QStringLiteral("^[0-9a-f]{64}$")).match(source.sha256).hasMatch())
        throw Error(ErrorCode::IntegrityFailure,"The embedded model range is invalid");
    iiFileProvider::native::Device file(false);
    file.open(path,{.read=true,.noFollow=true});
    if (!file.seek(std::int64_t(source.offset))) throw Error(ErrorCode::StorageFailure,"Cannot seek to the embedded model");
    QCryptographicHash digest(QCryptographicHash::Sha256);
    std::vector<char> buffer(1024*1024); quint64 remaining=source.size;
    while (remaining) {
        cancel.throwIfCancelled();
        const auto requested=std::int64_t(std::min<quint64>(remaining,buffer.size()));
        const auto count=file.read(buffer.data(),requested);
        if (count<=0 || count>requested) throw Error(ErrorCode::IntegrityFailure,"The embedded model changed while reading");
        digest.addData(QByteArrayView(buffer.data(),qsizetype(count)));
        remaining-=quint64(count);
    }
    if (QString::fromLatin1(digest.result().toHex())!=source.sha256)
        throw Error(ErrorCode::IntegrityFailure,"The embedded model checksum does not match its package");
    verifyPath(root,path);
    if (iiFileProvider::ObjectSource::inspect(path).stamp!=before.stamp)
        throw Error(ErrorCode::IntegrityFailure,"The Society model changed during verification");
    return before;
}

/// Adapts a metadata-only catalog entry to an independently verified GGUF
/// component while leaving device choice and execution with iiLocalLLM.
class EmbeddedModelRuntime final : public Runtime {
public:
    /// Bind host-selected sources to one direct Models root and llama policy.
    EmbeddedModelRuntime(QString root,QHash<QString,EmbeddedModelSource> sources,std::shared_ptr<Runtime> delegate)
        : root_(native(root)),sources_(std::move(sources)),delegate_(std::move(delegate)) {
        if (!delegate_ || !root_.is_absolute()) throw Error(ErrorCode::InvalidArgument,"An embedded model runtime requires a Models root and delegate");
    }
    /// Distinguish this adapter from the service's ordinary GGUF runtime.
    QString id() const override { return QStringLiteral("society-embedded-gguf"); }
    /// Admit only catalog references that the host associated with a source.
    bool supportsModel(const ModelSpec &spec) const override {
        return spec.format==QStringLiteral("society-embedded-gguf") && sources_.contains(spec.id);
    }
    /// Preserve the delegate's hardware selection and available device policy.
    QList<RuntimeDevice> devices(const HardwareInfo &hardware) const override { return delegate_->devices(hardware); }
    /// Reserve memory using metadata from the bounded source model range.
    MemoryEstimate estimateMemory(const ModelSpec &spec,int contextSlots) const override {
        auto translated=translate(spec);
        if (!delegate_->supportsModel(translated)) throw Error(ErrorCode::RuntimeUnavailable,"The installed iiLocalLLM cannot read embedded GGUF models");
        return delegate_->estimateMemory(translated,contextSlots);
    }
    /// Verify the source before loading and reject a source changed during load.
    std::shared_ptr<RuntimeModel> load(const ModelSpec &spec,const RuntimeDevice &device,
                                      const CancellationToken &cancel) override {
        const auto source=sources_.value(spec.id);
        auto translated=translate(spec);
        if (!delegate_->supportsModel(translated)) throw Error(ErrorCode::RuntimeUnavailable,"The installed iiLocalLLM cannot read embedded GGUF models");
        const auto before=verifySource(root_,source,cancel);
        auto result=delegate_->load(translated,device,cancel);
        cancel.throwIfCancelled(); verifyPath(root_,native(source.path));
        if (iiFileProvider::ObjectSource::inspect(native(source.path)).stamp!=before.stamp)
            throw Error(ErrorCode::IntegrityFailure,"The Society model changed during loading");
        return result;
    }
private:
    /// Only the host-owned source can introduce range options into the delegate.
    ModelSpec translate(const ModelSpec &spec) const {
        if (!supportsModel(spec) || !spec.projectorPath.isEmpty() ||
            spec.options.contains("__iilocal_gguf_offset") || spec.options.contains("__iilocal_gguf_size"))
            throw Error(ErrorCode::InvalidArgument,"Invalid embedded model request");
        const auto source=sources_.value(spec.id); verifyPath(root_,native(source.path));
        constexpr quint64 maxExactJson=9007199254740991ULL;
        if (!source.size || source.offset>maxExactJson || source.size>maxExactJson)
            throw Error(ErrorCode::InvalidArgument,"The embedded model range is invalid");
        auto translated=spec;
        translated.path=source.path; translated.format=QStringLiteral("gguf");
        translated.options.insert("__iilocal_gguf_offset",qint64(source.offset));
        translated.options.insert("__iilocal_gguf_size",qint64(source.size));
        return translated;
    }
    fs::path root_;
    QHash<QString,EmbeddedModelSource> sources_;
    std::shared_ptr<Runtime> delegate_;
};
}

std::shared_ptr<iiLocalLLM::Runtime> createEmbeddedModelRuntime(
        QString modelsRoot,QHash<QString,EmbeddedModelSource> sources,
        std::shared_ptr<iiLocalLLM::Runtime> delegate) {
    return std::make_shared<EmbeddedModelRuntime>(std::move(modelsRoot),std::move(sources),std::move(delegate));
}

bool removeVerifiedLegacyModelCache(QString modelsRoot,QString catalogRoot,
                                    const EmbeddedModelSource &source,std::stop_token stop) {
    try {
        if (stop.stop_requested() || !QRegularExpression(QStringLiteral("^[0-9a-f]{64}$")).match(source.sha256).hasMatch()) return false;
        const auto catalog=native(catalogRoot);
        std::error_code error;
        if (!catalog.is_absolute() || catalog.lexically_normal()!=catalog || fs::canonical(catalog,error)!=catalog || error) return false;
        const auto id=QStringLiteral("iisaccson-")+source.sha256;
        const auto directory=catalog/native(id);
        const auto status=fs::symlink_status(directory,error);
        if (error || !fs::is_directory(status) || fs::is_symlink(status) || fs::canonical(directory,error)!=directory || error) return false;
        const auto directoryName=QString::fromStdU16String(directory.u16string());
        const auto exactEntries=[&] {
            const auto names=QDir(directoryName).entryList(QDir::AllEntries|QDir::Hidden|QDir::System|QDir::NoDotAndDotDot);
            return names.size()==2 && names.contains(QStringLiteral("manifest.json")) && names.contains(QStringLiteral("model.gguf"));
        };
        if (!exactEntries()) return false;
        const auto manifestPath=directory/"manifest.json",modelPath=directory/"model.gguf";
        verifyPath(catalog,manifestPath); verifyPath(catalog,modelPath);
        const auto manifestBefore=iiFileProvider::ObjectSource::inspect(manifestPath);
        if (!manifestBefore.size || manifestBefore.size>1024*1024) return false;
        iiFileProvider::native::Device file(false);
        file.open(manifestPath,{.read=true,.noFollow=true});
        QByteArray bytes(qsizetype(manifestBefore.size),Qt::Uninitialized);
        qint64 read=0;
        while (read<bytes.size()) {
            const auto count=file.read(bytes.data()+read,bytes.size()-read);
            if (count<=0) return false;
            read+=count;
        }
        file.close();
        QJsonParseError parse;
        const auto document=QJsonDocument::fromJson(bytes,&parse);
        if (parse.error!=QJsonParseError::NoError || !document.isObject()) return false;
        const auto manifest=document.object();
        const auto keys=manifest.keys();
        const QSet<QString> manifestKeys{"schema_version","id","architecture","format","quantization","context_length","capabilities","entry_point","files"};
        if (QSet<QString>(keys.cbegin(),keys.cend())!=manifestKeys ||
            manifest.value("schema_version")!=1 || manifest.value("id")!=id ||
            !manifest.value("architecture").isString() || manifest.value("architecture").toString().isEmpty() ||
            manifest.value("format")!="gguf" || manifest.value("quantization")!="unknown" ||
            manifest.value("context_length")!=2048 || manifest.value("capabilities")!=QJsonArray{"chat"} ||
            manifest.value("entry_point")!="model.gguf") return false;
        const auto files=manifest.value("files").toArray();
        if (files.size()!=1 || !files.first().isObject()) return false;
        const auto record=files.first().toObject();
        const auto fileKeys=record.keys();
        const QSet<QString> recordKeys{"path","size","sha256"};
        if (QSet<QString>(fileKeys.cbegin(),fileKeys.cend())!=recordKeys || record.value("path")!="model.gguf" ||
            record.value("sha256")!=source.sha256 || record.value("size").toInteger(-1)<0 ||
            quint64(record.value("size").toInteger(-1))!=source.size) return false;
        iiLocalLLM::CancellationToken cancellation;
        std::stop_callback onStop(stop,[cancellation] { cancellation.cancel(); });
        const auto originalBefore=verifySource(native(modelsRoot),source,cancellation);
        const EmbeddedModelSource cached{QString::fromStdU16String(modelPath.u16string()),source.sha256,0,source.size};
        const auto modelBefore=verifySource(catalog,cached,cancellation);
        if (modelBefore.size!=source.size || stop.stop_requested() || !exactEntries()) return false;
        verifyPath(catalog,manifestPath); verifyPath(catalog,modelPath); verifyPath(native(modelsRoot),native(source.path));
        if (iiFileProvider::ObjectSource::inspect(manifestPath).stamp!=manifestBefore.stamp ||
            iiFileProvider::ObjectSource::inspect(modelPath).stamp!=modelBefore.stamp ||
            iiFileProvider::ObjectSource::inspect(native(source.path)).stamp!=originalBefore.stamp) return false;
        // Delete only the two verified leaves, never recursively traverse a cache.
        if (!fs::remove(modelPath,error) || error) return false;
        if (!fs::remove(manifestPath,error) || error) return false;
        return fs::remove(directory,error) && !error;
    } catch (const std::exception &) {
        // A changed, cancelled or unreadable cache remains available for review.
        return false;
    }
}
