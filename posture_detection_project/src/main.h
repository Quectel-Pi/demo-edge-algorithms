#pragma once
/* main.h — 应用入口配置 */
#include <QString>

namespace appconfig {

/* 读环境变量；未设置/为空时返回默认值 */
QString readEnv(const char* name, const QString& fallback);

/* 常用项默认值（可被环境变量覆盖，run.sh 中统一注入） */
extern const QString kDefaultModel;   /* 模型 .engine 路径 */
extern const int     kDefaultModule;  /* 算法板型：RK3576 = 3 */

}  // namespace appconfig