#include "app_session_export.h"
#include <QFile>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QCryptographicHash>
#include <QUuid>

static bool app_export_fail(QString *error,const QString &reason) { if(error) *error=reason; return false; }
static QByteArray app_export_file_hash(QFile &file)
{
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if(!hash.addData(&file)) return QByteArray();
    return hash.result();
}
bool app_session_validate_directory(const QString &path,QString *error,bool require_stopped)
{
    const QFileInfo directory(path);
    if(path.isEmpty() ||!directory.isDir() ||directory.isSymLink()) return app_export_fail(error,"Session directory is missing or is a symbolic link");
    const QDir dir(path); QFile metadata(dir.filePath("metadata.json"));
    if(!metadata.open(QIODevice::ReadOnly)) return app_export_fail(error,"Cannot read session metadata: "+metadata.errorString());
    const auto document=QJsonDocument::fromJson(metadata.readAll()); const auto state=document.object();
    if(!document.isObject() ||state["application_id"].toString()!="org.temphumi.monitor"
       ||state["session_id"].toString()!=directory.fileName()) return app_export_fail(error,"Directory is not an identified TempHumi session");
    if(!QFileInfo::exists(dir.filePath("raw_input.log")) ||!QFileInfo::exists(dir.filePath("parsed.csv")))
        return app_export_fail(error,"Session is incomplete");
    if(state["event_file"]=="session_events.jsonl" &&!QFileInfo::exists(dir.filePath("session_events.jsonl")))
        return app_export_fail(error,"Session display event sidecar is missing");
    if(require_stopped && (state["active"].toBool() ||state["session_end_wall_time"].toString().isEmpty()
       ||!state["recording_error"].toString().isEmpty())) return app_export_fail(error,"Stop a complete, successful recording before exporting");
    return true;
}
bool app_session_export_directory(const QString &source,const QString &parent,QString *destination,QString *error)
{
    if(!app_session_validate_directory(source,error,true)) return false;
    const QFileInfo target(parent); const QFileInfo original(source);
    if(!target.isDir()) return app_export_fail(error,"Choose an existing writable export directory");
    const QString canonical=original.canonicalFilePath(); const QString chosen=target.canonicalFilePath();
    if(chosen==canonical ||chosen.startsWith(canonical+"/",Qt::CaseInsensitive))
        return app_export_fail(error,"Export destination cannot be inside the canonical session");
    // Build an independent, uniquely named export. QTemporaryDir removes only
    // this attempt's partial copies on failure; original evidence stays intact.
    QTemporaryDir staging(QDir(parent).filePath(".temphumi_export_XXXXXX"));
    if(!staging.isValid()) return app_export_fail(error,"Cannot create export directory: permission or path error");
    QStringList names={"raw_input.log","parsed.csv","metadata.json"};
    if(QFileInfo::exists(QDir(source).filePath("command_tx.jsonl"))) names.append("command_tx.jsonl");
    if(QFileInfo::exists(QDir(source).filePath("session_events.jsonl"))) names.append("session_events.jsonl");
    for(const auto &name:names) {
        const QString from=QDir(source).filePath(name),to=QDir(staging.path()).filePath(name);
        if(QFileInfo(from).isSymLink()) return app_export_fail(error,"Session contains a linked evidence file");
        if(!QFile::copy(from,to)) return app_export_fail(error,"Cannot copy "+name+": permissions, lock or space error");
        QFile a(from),b(to);
        if(!a.open(QIODevice::ReadOnly) ||!b.open(QIODevice::ReadOnly) ||a.size()!=b.size())
            return app_export_fail(error,"Cannot verify export size/readability: "+name);
        const auto hash=app_export_file_hash(a);
        if(hash.isEmpty() ||hash!=app_export_file_hash(b)) return app_export_fail(error,"Export SHA-256 mismatch: "+name);
    }
    const QString name=original.fileName()+"_export_"+QUuid::createUuid().toString(QUuid::Id128).left(8);
    const QString finalPath=QDir(parent).filePath(name);
    if(QFileInfo::exists(finalPath) ||!QDir().rename(staging.path(),finalPath))
        return app_export_fail(error,"Cannot finalize export without overwriting existing files");
    staging.setAutoRemove(false); if(destination) *destination=finalPath; return true;
}
