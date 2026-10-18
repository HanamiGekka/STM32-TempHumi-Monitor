#ifndef APP_VERSION_H
#define APP_VERSION_H

#ifdef __cplusplus
extern "C" {
#endif

#include "app_project_version.h"
#define APP_VERSION_STR       APP_PROJECT_VERSION_STR
#define APP_VERSION_DATE      "2026-10-14"
#define APP_VERSION_FULL_STR  APP_VERSION_STR " (" APP_VERSION_DATE ")"

#ifdef __cplusplus
}
#endif

#endif /* APP_VERSION_H */
