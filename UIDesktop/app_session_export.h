#ifndef APP_SESSION_EXPORT_H
#define APP_SESSION_EXPORT_H
#include <QString>
bool app_session_validate_directory(const QString &path,QString *error,bool require_stopped=false);
bool app_session_export_directory(const QString &source,const QString &parent,QString *destination,QString *error);
#endif
