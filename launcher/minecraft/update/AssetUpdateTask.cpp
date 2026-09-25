#include "AssetUpdateTask.h"

#include "BuildConfig.h"
#include "launch/LaunchStep.h"
#include "minecraft/AssetsUtils.h"
#include "minecraft/MinecraftInstance.h"
#include "minecraft/PackProfile.h"
#include "net/ChecksumValidator.h"

#include "Application.h"

#include "net/ApiRequest.h"

AssetUpdateTask::AssetUpdateTask(MinecraftInstance* inst)
{
    m_inst = inst;
}

void AssetUpdateTask::executeTask()
{
    setStatus(tr("Updating assets index..."));
    auto components = m_inst->getPackProfile();
    auto profile = components->getProfile();
    auto assets = profile->getMinecraftAssets();
    QUrl indexUrl = assets->url;
    QString localPath = assets->id + ".json";

    auto metacache = APPLICATION->metacache();
    auto entry = metacache->resolveEntry("asset_indexes", localPath);

    QString asset_fname = "assets/indexes/" + assets->id + ".json";
    bool needDownload = entry->isStale() || !QFile::exists(asset_fname);

    if (needDownload) {
        auto job = makeShared<NetJob>(tr("Asset index for %1").arg(m_inst->name()), APPLICATION->network());
        auto hexSha1 = assets->sha1.toLatin1();
        qDebug() << "Asset index SHA1:" << hexSha1;
        Net::Request::Options options = Net::Request::Option::MakeEternal;
        auto dl = Net::ApiRequest::makeCached(indexUrl, entry, options);
        dl->addValidator(new Net::ChecksumValidator(QCryptographicHash::Sha1, assets->sha1));
        job->addNetAction(dl);

        downloadJob.reset(job);

        connect(downloadJob.get(), &NetJob::succeeded, this, &AssetUpdateTask::assetIndexFinished);
        connect(downloadJob.get(), &NetJob::failed, this, &AssetUpdateTask::assetIndexFailed);
        connect(downloadJob.get(), &NetJob::aborted, this, &AssetUpdateTask::emitAborted);
        connect(downloadJob.get(), &NetJob::progress, this, &AssetUpdateTask::progress);
        connect(downloadJob.get(), &NetJob::stepProgress, this, &AssetUpdateTask::propagateStepProgress);

        qDebug() << "Starting asset index download for" << m_inst->name();
        downloadJob->start();
    } else {
        qDebug() << "Asset index already cached for" << m_inst->name();
        assetIndexFinished();
    }
}

bool AssetUpdateTask::canAbort() const
{
    return true;
}

void AssetUpdateTask::assetIndexFinished()
{
    AssetsIndex index;
    qDebug() << "Finished asset index download for" << m_inst->name();

    auto components = m_inst->getPackProfile();
    auto profile = components->getProfile();
    auto assets = profile->getMinecraftAssets();

    QString asset_fname = "assets/indexes/" + assets->id + ".json";
    QString verified_fname = "assets/indexes/" + assets->id + ".verified";

    if (!assets->sha1.isEmpty() && QFile::exists(verified_fname)) {
        QFile vfile(verified_fname);
        if (vfile.open(QIODevice::ReadOnly)) {
            const QString cachedSha1 = QString::fromUtf8(vfile.readAll()).trimmed();
            if (cachedSha1 == assets->sha1) {
                emitSucceeded();
                return;
            }
        }
    }

    // FIXME: this looks like a job for a generic validator based on json schema?
    if (!AssetsUtils::loadAssetsIndexJson(assets->id, asset_fname, index)) {
        auto metacache = APPLICATION->metacache();
        auto entry = metacache->resolveEntry("asset_indexes", assets->id + ".json");
        metacache->evictEntry(entry);
        emitFailed(tr("Failed to read the assets index!"));
        return;
    }

    auto writeVerifiedStamp = [verified_fname, expectedSha1 = assets->sha1]() {
        if (expectedSha1.isEmpty()) {
            return;
        }
        QFile vfile(verified_fname);
        if (vfile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            vfile.write(expectedSha1.toUtf8());
        }
    };

    auto job = index.getDownloadJob();
    if (job) {
        QString resourceURL = resourceUrl();
        QString source = tr("Mojang");
        if (resourceURL != BuildConfig.DEFAULT_RESOURCE_BASE) {
            source = QUrl(resourceURL).host();
        }
        setStatus(tr("Getting the asset files from %1...").arg(source));
        downloadJob = job;
        connect(downloadJob.get(), &NetJob::succeeded, this, [this, writeVerifiedStamp]() {
            writeVerifiedStamp();
            emitSucceeded();
        });
        connect(downloadJob.get(), &NetJob::failed, this, &AssetUpdateTask::assetsFailed);
        connect(downloadJob.get(), &NetJob::aborted, this, &AssetUpdateTask::emitAborted);
        connect(downloadJob.get(), &NetJob::progress, this, &AssetUpdateTask::progress);
        connect(downloadJob.get(), &NetJob::stepProgress, this, &AssetUpdateTask::propagateStepProgress);
        downloadJob->start();
        return;
    }
    writeVerifiedStamp();
    emitSucceeded();
}

void AssetUpdateTask::assetIndexFailed(QString reason)
{
    qDebug() << m_inst->name() << ": Failed asset index download:" << reason;
    auto components = m_inst->getPackProfile();
    auto profile = components->getProfile();
    auto assets = profile->getMinecraftAssets();
    QString asset_fname = "assets/indexes/" + assets->id + ".json";
    if (QFile::exists(asset_fname)) {
        qWarning() << "Failed to download asset index, but local file exists. Continuing with cached index.";
        assetIndexFinished();
        return;
    }
    emitFailed(tr("Failed to download the assets index:\n%1").arg(reason));
}

void AssetUpdateTask::assetsFailed(QString reason)
{
    emitFailed(tr("Failed to download assets:\n%1").arg(reason));
}

bool AssetUpdateTask::abort()
{
    if (downloadJob) {
        return downloadJob->abort();
    } else {
        qWarning() << "Prematurely aborted AssetUpdateTask";
    }
    return true;
}

QString AssetUpdateTask::resourceUrl()
{
    if (const QString urlOverride = APPLICATION->settings()->get("ResourceURLOverride").toString(); !urlOverride.isEmpty()) {
        return urlOverride;
    }

    return BuildConfig.DEFAULT_RESOURCE_BASE;
}
