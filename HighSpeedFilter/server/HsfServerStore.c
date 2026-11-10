#include "HsfServerStore.h"

#if XSQL_ON

#include "XSql.h"
#include "XSqlDatabase.h"
#include "XSqlQuery.h"
#include "XSqlError.h"
#include "XDateTime.h"
#include "XString.h"
#include "XMemory.h"
#include "XPrintf.h"
#include "HsfServerSettings.h"
#include <stdio.h>
#include <string.h>

/* ==================== 单例连接名 ==================== */

#define HSF_STORE_CONN_NAME "hsf_store"

/**
 * @brief 合法墙钟毫秒下界（2020-01-01；与 Python VALID_TS_MIN 同值）。
 * @details history 与 logs 两张表的查询共用（[2026-11-09 移位] 原先只挂在
 *          「历史分页查询」小节，logs 查询插在它前面后按源序取不到宏——
 *          移到文件头部共用区）。
 */
#define HSF_STORE_VALID_TS_MIN  1577836800000LL

/* ==================== 内部工具 ==================== */

/** @brief 前置声明：查询首行首列取整数（COUNT/MIN/MAX 用；见文件后段）。 */
static bool storeQueryInt(HsfServerStore* st, const char* sql, int64_t* out);

/**
 * @brief 单引号 SQL 字面量转义并写入缓冲。
 * @details ' → ''（SQLite 标准转义）。其余字符（中文/百分号等）原样保留。
 */
static void storeSqlQuote(char* out, int cap, const char* text)
{
    int o = 0;
    int i;
    if (!out || cap <= 0) return;
    out[0] = '\0';
    if (!text) return;
    for (i = 0; text[i] != '\0'; ++i) {
        if (o >= cap - 2) break;
        if (text[i] == '\'') {
            if (o >= cap - 3) break;
            out[o++] = '\'';
            out[o++] = '\'';
        } else {
            out[o++] = text[i];
        }
    }
    out[o] = '\0';
}

/** @brief 可空实数列 → "NULL" 或定点文本。 */
static void storeNum(char* out, int cap, double v, bool valid)
{
    if (!valid) {
        snprintf(out, (size_t)cap, "NULL");
    } else {
        snprintf(out, (size_t)cap, "%.4f", v);
    }
}

/** @brief 可空文本列 → "'…'" 或 NULL。 */
static void storeText(char* out, int cap, const char* text)
{
    if (!text) {
        snprintf(out, (size_t)cap, "NULL");
    } else {
        char quoted[160];
        storeSqlQuote(quoted, (int)sizeof(quoted), text);
        snprintf(out, (size_t)cap, "'%s'", quoted);
    }
}

/** @brief 执行一条 SQL；失败打一次日志（同类失败不刷屏）。 */
static bool storeExec(HsfServerStore* st, const char* sql)
{
    XSqlQuery* q = XSqlDatabase_exec_utf8(st->m_db, sql);
    if (!q) return false;
    if (!XSqlQuery_isActive(q)) {
        XString* text = XSqlQuery_lastQuery(q);
        XPrintf("HsfServerStore: SQL 失败：%s\n",
                text ? XString_toUtf8(text) : sql);
        if (text) XClassDelete((XClass*)text);
        XClassDelete((XClass*)q);
        return false;
    }
    XClassDelete((XClass*)q);
    return true;
}

/* ==================== 构造 / 析构 ==================== */

/**
 * @brief 老库补 history.water_temp 列（2026-11-10，水温列落地）。
 * @details [为什么要迁移] `CREATE TABLE IF NOT EXISTS` 对**已存在**的表
 *          不做任何改动 ⇒ 现场已有 hsf_data.db 永远补不上 water_temp，
 *          而历史查询的 SELECT 里点名了这一列 ⇒ 查询整体失败、历史页
 *          一行都取不到（不是"少一列"而是"整页空"）。
 *          [先探测再改] SQLite 的 ADD COLUMN 不幂等（第二次报 duplicate
 *          column name），先查 PRAGMA table_info。
 *          [失败不致命] 补列失败只是水温列不可用；为不让整页挂掉，
 *          查询侧对旧库仍走同一条 SQL —— 故失败时这里必须打印明确日志。
 * @note  与 Python store._migrate_history_water_temp 同口径。
 */
static void storeEnsureHistoryWaterTemp(HsfServerStore* st)
{
    XSqlQuery* q;
    bool has = false;

    if (!st || !st->m_db) return;
    q = XSqlDatabase_exec_utf8(st->m_db, "PRAGMA table_info(history)");
    if (q) {
        if (XSqlQuery_isActive(q) && XSqlQuery_first(q)) {
            do {
                XSqlRecord* rec = XSqlQuery_record(q);
                const XVariant* var;
                if (!rec) break;
                /* 列 1 = 列名（PRAGMA table_info: cid,name,type,...）。 */
                var = XSqlRecord_value_const(rec, 1);
                if (var && XVariant_isValid(var)) {
                    const XString* name = XVariant_toString(var);
                    const char* u8 = name ? XString_toUtf8(name) : NULL;
                    if (u8 && strcmp(u8, "water_temp") == 0) has = true;
                }
                XClassDelete((XClass*)rec);
                if (has) break;
            } while (XSqlQuery_next(q));
        }
        XClassDelete((XClass*)q);
    }
    if (has) return;
    if (storeExec(st, "ALTER TABLE history ADD COLUMN water_temp REAL")) {
        XPrintf("HsfServerStore: history 表已补 water_temp 列（水温）\n");
    } else {
        XPrintf("HsfServerStore: history 表补 water_temp 列失败"
                "（历史页可能取不到数据）\n");
    }
}

static void VXHsfServerStore_deinit(XObject* obj)
{
    HsfServerStore* st = (HsfServerStore*)obj;
    if (!st) return;
    HsfServerStore_close(st);
    if (st->m_path) {
        XClassDeinit(st->m_path);
        XFree_System(st->m_path);
        st->m_path = NULL;
    }
}

XVtable* HsfServerStore_class_init(void)
{
    XVTABLE_INIT_DEFAULT(HsfServerStore)
    XVTABLE_INHERIT_XCLASS(XObject);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXHsfServerStore_deinit);
    return XVTABLE_DEFAULT;
}

void HsfServerStore_init(HsfServerStore* st)
{
    if (!st) return;
    memset(st, 0, sizeof(*st));
    XObject_init(st);
    XClassSetVtable(st, HsfServerStore);
    st->m_enabled = false;
    st->m_sampleMs = HSF_STORE_SAMPLE_MS;
    st->m_db = NULL;
    st->m_lastSampleMs = 0;
    st->m_openLogged = false;
}

void HsfServerStore_clear(HsfServerStore* st)
{
    if (!st) return;
    HsfServerStore_close(st);
    XClassDeinit(st);
}

bool HsfServerStore_configure(HsfServerStore* st, const char* baseDir)
{
    char path[512];
    if (!st) return false;
    /* [缺失键读回是垃圾不是 def] XSettings INI 后端对缺失键返回非空
     * 空壳变体（getInt 吞默认值恒 0、getString 得 "0"）——历史实锅
     * （见 XSettings INI 后端四类坑）。一切"可选配置"必须先 contains
     * 判存在，再读值；缺省值只在**落盘种子**里出现。 */
    st->m_enabled = HsfServerSettings_contains("storage/enabled")
                        ? (HsfServerSettings_getInt("storage/enabled", 1) != 0)
                        : true;
    st->m_sampleMs =
        HsfServerSettings_contains("storage/sample_ms")
            ? HsfServerSettings_getInt("storage/sample_ms",
                                       HSF_STORE_SAMPLE_MS)
            : HSF_STORE_SAMPLE_MS;
    if (st->m_sampleMs < 1000) st->m_sampleMs = 1000;

    /* [路径缺省不落 INI] 缺省路径由数据目录现拼（部署目录一变 INI 里的
     * 绝对路径就成了坑）；只有现场显式配置过才用 INI 值。 */
    path[0] = '\0';
    if (HsfServerSettings_contains("storage/path")) {
        HsfServerSettings_getString("storage/path", "", path,
                                    (int)sizeof(path));
        /* [自愈] 历史坏键（被缺省值坑写成 "0"）视为未配置。 */
        if (path[0] == '\0' || strcmp(path, "0") == 0) path[0] = '\0';
    }
    if (path[0] == '\0') {
        snprintf(path, sizeof(path), "%s/%s", baseDir ? baseDir : ".",
                 HSF_STORE_DB_NAME);
        /* 首次运行把生效配置落 INI（现场看得见、可改）。 */
        HsfServerSettings_setString("storage/path", path);
        HsfServerSettings_setInt("storage/enabled", st->m_enabled ? 1 : 0);
        HsfServerSettings_setInt("storage/sample_ms", st->m_sampleMs);
    }
    if (st->m_path) {
        XClassDeinit(st->m_path);
        XFree_System(st->m_path);
        st->m_path = NULL;
    }
    st->m_path = (XString*)XMemory_malloc(sizeof(XString),
                                          XCLASS_DEFAULT_MEMORY_TYPE);
    if (st->m_path) {
        XString_init(st->m_path);
        XString_assign_utf8(st->m_path, path);
    }
    return st->m_enabled;
}

bool HsfServerStore_open(HsfServerStore* st)
{
    XString* connName;
    if (!st || !st->m_enabled) return false;
    if (st->m_db) return true;
    if (!st->m_path) return false;

    XSqliteDriver_register();
    connName = XString_create_utf8(HSF_STORE_CONN_NAME);
    st->m_db = XSqlDatabase_database_2(HSF_STORE_CONN_NAME, false);
    if (!st->m_db) {
        st->m_db = XSqlDatabase_addDatabase_2(XSqlDriverType_Sqlite,
                                              HSF_STORE_CONN_NAME);
    }
    if (st->m_db) {
        XSqlDatabase_setDatabaseName(st->m_db, st->m_path);
        if (!XSqlDatabase_open(st->m_db)) {
            XPrintf("HsfServerStore: 打开库失败（路径 %s）——继续运行，"
                    "只是不落盘\n", XString_toUtf8(st->m_path));
            st->m_db = NULL;
        }
    }
    if (st->m_db) {
        /* WAL：写不锁读；建表语句与 Python _SCHEMA 逐列一致。 */
        storeExec(st, "PRAGMA journal_mode=WAL");
        storeExec(st,
                  "CREATE TABLE IF NOT EXISTS history ("
                  " ts INTEGER PRIMARY KEY, mode TEXT, vfd_state TEXT,"
                  " freq_hz REAL, current_a REAL, rpm REAL,"
                  " turb1 REAL, turb2 REAL, flow REAL, flow_unit TEXT,"
                  " flow_total REAL, total_unit TEXT, press_mpa REAL,"
                  " press_note TEXT, temp REAL, hum REAL,"
                  " di INTEGER, do INTEGER, water_temp REAL)");
        storeExec(st, "CREATE INDEX IF NOT EXISTS idx_history_ts ON history(ts)");
        storeExec(st,
                  "CREATE TABLE IF NOT EXISTS alarms ("
                  " ts INTEGER, kind TEXT, source TEXT, text TEXT)");
        storeExec(st, "CREATE INDEX IF NOT EXISTS idx_alarms_ts ON alarms(ts)");
        /* 运行日志表（HSF_CMD_LOG_PAGE=0x0119 数据源）。列名对齐 Python
         * store.py 的 logs(ts, text)，category 是 C 端扩展列（类别筛选/
         * 下拉用，入库口径见 HsfServerStore.h HsfLogRow）。 */
        storeExec(st,
                  "CREATE TABLE IF NOT EXISTS logs ("
                  " ts INTEGER, category TEXT, text TEXT)");
        storeExec(st, "CREATE INDEX IF NOT EXISTS idx_logs_ts ON logs(ts)");
        /* [老库补列 2026-11-10] 现场已有 hsf_data.db 的 history 表没有
         * water_temp 列（CREATE TABLE IF NOT EXISTS 对已存在的表不动）⇒
         * 不补的话历史查询 SELECT 里点名的这一列会报错、整页取不到数据。
         * ADD COLUMN 不幂等，先探测再改（与 Python store 的
         * _migrate_history_water_temp 同口径）。 */
        storeEnsureHistoryWaterTemp(st);
        if (!st->m_openLogged) {
            st->m_openLogged = true;
            XPrintf("HsfServerStore: 落盘已启用（%s，采样 %dms，"
                    "history+alarms+logs 三表）\n",
                    XString_toUtf8(st->m_path), st->m_sampleMs);
        }
    }
    if (connName) XClassDelete((XClass*)connName);
    return st->m_db != NULL;
}

void HsfServerStore_close(HsfServerStore* st)
{
    if (!st || !st->m_db) return;
    XSqlDatabase_close(st->m_db);
    st->m_db = NULL;
}

bool HsfServerStore_isOpen(const HsfServerStore* st)
{
    return st && st->m_db != NULL;
}

bool HsfServerStore_enabled(const HsfServerStore* st)
{
    return st && st->m_enabled;
}

int HsfServerStore_sampleMs(const HsfServerStore* st)
{
    return st ? st->m_sampleMs : HSF_STORE_SAMPLE_MS;
}

const char* HsfServerStore_dbPath(const HsfServerStore* st)
{
    if (!st || !st->m_path) return "";
    return XString_toUtf8(st->m_path);
}

int64_t HsfServerStore_usedBytes(const HsfServerStore* st)
{
    int64_t total = 0;
    static const char* const kSuffix[] = { "", "-wal", "-shm" };
    int i;

    if (!st || !st->m_path) return 0;
    /* [主库 + WAL + shm] 磁盘真实占用（现场 du 看到的就是它）。 */
    for (i = 0; i < (int)(sizeof(kSuffix) / sizeof(kSuffix[0])); ++i) {
        char path[512];
        FILE* f;
        long size = 0;
        snprintf(path, sizeof(path), "%s%s", XString_toUtf8(st->m_path),
                 kSuffix[i]);
        f = fopen(path, "rb");
        if (!f) continue;
        if (fseek(f, 0, SEEK_END) == 0) {
            size = ftell(f);
            if (size > 0) total += (int64_t)size;
        }
        fclose(f);
    }
    return total;
}

int64_t HsfServerStore_rowCount(const HsfServerStore* st)
{
    char sql[64];
    int64_t n = 0;

    if (!st || !st->m_db) return 0;
    snprintf(sql, sizeof(sql), "SELECT COUNT(*) FROM history");
    if (!storeQueryInt(st, sql, &n)) return 0;
    return n;
}

/* ==================== 写入 ==================== */

bool HsfServerStore_insertHistory(HsfServerStore* st, const HsfHistoryRow* row)
{
    char sql[1024];
    char cMode[64];
    char cVfd[80];
    char cFreq[32];
    char cCur[32];
    char cRpm[32];
    char cT1[32];
    char cT2[32];
    char cFlow[32];
    char cFU[32];
    char cTotal[32];
    char cTU[32];
    char cPress[32];
    char cPN[96];
    char cTemp[32];
    char cHum[32];
    char cWTemp[32];

    if (!st || !st->m_db || !row) return false;

    storeText(cMode, (int)sizeof(cMode), row->mode);
    storeText(cVfd, (int)sizeof(cVfd), row->vfdState);
    storeNum(cFreq, (int)sizeof(cFreq), row->freqHz, row->freqHzValid);
    storeNum(cCur, (int)sizeof(cCur), row->currentA, row->currentAValid);
    storeNum(cRpm, (int)sizeof(cRpm), row->rpm, row->rpmValid);
    storeNum(cT1, (int)sizeof(cT1), row->turb1, row->turb1Valid);
    storeNum(cT2, (int)sizeof(cT2), row->turb2, row->turb2Valid);
    storeNum(cFlow, (int)sizeof(cFlow), row->flow, row->flowValid);
    storeText(cFU, (int)sizeof(cFU), row->flowUnit);
    storeNum(cTotal, (int)sizeof(cTotal), row->flowTotal, row->flowTotalValid);
    storeText(cTU, (int)sizeof(cTU), row->totalUnit);
    storeNum(cPress, (int)sizeof(cPress), row->pressMpa, row->pressValid);
    storeText(cPN, (int)sizeof(cPN), row->pressNote);
    storeNum(cTemp, (int)sizeof(cTemp), row->temp, row->tempValid);
    storeNum(cHum, (int)sizeof(cHum), row->hum, row->humValid);
    /* [水温] 带温度的机型（浊度仪2）才有；没有就写 NULL（"没数据"），
     * 不写 0 —— 0℃ 会被当成真实读数。 */
    storeNum(cWTemp, (int)sizeof(cWTemp), row->waterTemp,
             row->waterTempValid);

    snprintf(sql, sizeof(sql),
             "INSERT OR REPLACE INTO history(ts,mode,vfd_state,freq_hz,"
             "current_a,rpm,turb1,turb2,flow,flow_unit,flow_total,"
             "total_unit,press_mpa,press_note,temp,hum,di,do,water_temp)"
             " VALUES("
             "%lld,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%u,%u,%s)",
             (long long)row->tsMs, cMode, cVfd, cFreq, cCur, cRpm, cT1, cT2,
             cFlow, cFU, cTotal, cTU, cPress, cPN, cTemp, cHum,
             row->diValid ? (unsigned)row->di : 0u,
             row->doValid ? (unsigned)row->doMask : 0u,
             cWTemp);
    /* [di/do 位图不写 NULL] "没读到"与"全 0"在位图口径下都落 0；行级
     * 新鲜度由同行的设备列（freq/temp/...）是否 NULL 表达，与界面
     * "--"（列级 NULL）一致。 */
    return storeExec(st, sql);
}

bool HsfServerStore_insertAlarm(HsfServerStore* st, const char* kind,
                                const char* source, const char* text)
{
    char sql[640];
    char cKind[32];
    char cSource[96];
    char cText[256];
    int64_t now;

    if (!st || !st->m_db) return false;
    storeText(cKind, (int)sizeof(cKind), kind);
    storeText(cSource, (int)sizeof(cSource), source);
    storeText(cText, (int)sizeof(cText), text);
    now = XDateTime_currentSecsSinceEpoch() * 1000LL;
    snprintf(sql, sizeof(sql),
             "INSERT INTO alarms(ts,kind,source,text) VALUES(%lld,%s,%s,%s)",
             (long long)now, cKind, cSource, cText);
    return storeExec(st, sql);
}

/* ==================== 运行日志（logs 表） ==================== */

bool HsfServerStore_insertLog(HsfServerStore* st, int64_t tsMs,
                              const char* category, const char* text)
{
    char sql[640];
    char cCat[24];
    char cText[256];
    int64_t now;

    if (!st || !st->m_db) return false;
    storeText(cCat, (int)sizeof(cCat), category ? category : "");
    storeText(cText, (int)sizeof(cText), text);
    /* [时刻缺省=现在] 调用方一般用事件发生那一刻的墙钟；<=0 视为"就地取"。 */
    now = (tsMs > 0) ? tsMs : XDateTime_currentSecsSinceEpoch() * 1000LL;
    snprintf(sql, sizeof(sql),
             "INSERT INTO logs(ts,category,text) VALUES(%lld,%s,%s)",
             (long long)now, cCat, cText);
    return storeExec(st, sql);
}

/**
 * @brief 拼日志关键字条件（口径照 storeBuildKwCondition）。
 * @param out 输出 SQL 片段（以 " AND (…)" 开头；"…" = 匹配列拼接）。
 * @param cap 输出缓冲容量。
 * @param kw 关键字（utf8；NULL/空=不筛，返回 0）。
 * @return 写入条件返回 1；无需筛选返回 0。
 * @details [匹配列] logs 表没有"显示表达式"一说，匹配列就是界面两列：
 *          text 与 category（时间列的文本口径随本地时区漂移，关键字搜
 *          时间不如直接用时间窗——故不纳入 LIKE 列）。单引号加倍 +
 *          LIKE 的 \\ % _ 前加反斜杠并声明 ESCAPE '\\'，与历史表同一套
 *          转义口径（用户输入 %/_ 不变成通配符）。
 */
static int storeBuildLogKwCondition(char* out, int cap, const char* kw)
{
    char lit[192];
    size_t n = 0;
    size_t kwLen;
    size_t k;
    int used;

    if (!out || cap <= 0) return 0;
    out[0] = '\0';
    if (!kw || !kw[0]) return 0;
    /* [与历史表同口径] 超长只取前 128 字符继续筛（不整条丢弃）。 */
    kwLen = strlen(kw);
    if (kwLen > 128) kwLen = 128;
    for (k = 0; k < kwLen && n < sizeof(lit) - 8; ++k) {
        unsigned char ch = (unsigned char)kw[k];
        if (ch == '\'') { lit[n++] = '\''; lit[n++] = '\''; }
        else if (ch == '\\' || ch == '%' || ch == '_') {
            lit[n++] = '\\';
            lit[n++] = (char)ch;
        } else {
            lit[n++] = (char)ch;
        }
    }
    lit[n] = '\0';
    used = snprintf(out, (size_t)cap,
                    " AND (COALESCE(text, '') || ' ' || "
                    "COALESCE(category, '')) LIKE '%%%s%%' ESCAPE '\\'", lit);
    return (used > 0 && used < cap) ? 1 : 0;
}

int HsfServerStore_queryLogs(HsfServerStore* st, int offset, int limit,
                             int64_t loMs, int64_t hiMs,
                             const char* category, const char* kw,
                             HsfLogRow* outRows, int outMaxRows,
                             int* outTotal)
{
    char sql[1200];
    char where[512];
    char kwCond[320];
    char catLit[24];
    XSqlQuery* q;
    XSqlRecord* rec;
    int written = 0;
    int64_t count = 0;
    int whereLen;

    if (outTotal) *outTotal = 0;
    if (!st || !st->m_db || !outRows || outMaxRows <= 0) return 0;
    if (offset < 0) offset = 0;
    if (limit <= 0) limit = 1;

    /* [时间窗] 与 queryHistory 同口径：只认有效墙钟（滤掉误写 monotonic
     * 的坏行），上界缺省不限。 */
    if (loMs > 0 && hiMs > 0) {
        snprintf(where, sizeof(where),
                 " WHERE ts >= %lld AND ts <= %lld AND ts >= %lld",
                 (long long)loMs, (long long)hiMs,
                 (long long)HSF_STORE_VALID_TS_MIN);
    } else if (loMs > 0) {
        snprintf(where, sizeof(where), " WHERE ts >= %lld AND ts >= %lld",
                 (long long)loMs, (long long)HSF_STORE_VALID_TS_MIN);
    } else if (hiMs > 0) {
        snprintf(where, sizeof(where), " WHERE ts <= %lld AND ts >= %lld",
                 (long long)hiMs, (long long)HSF_STORE_VALID_TS_MIN);
    } else {
        snprintf(where, sizeof(where), " WHERE ts >= %lld",
                 (long long)HSF_STORE_VALID_TS_MIN);
    }
    whereLen = (int)strlen(where);
    /* [类别精确匹配] 下拉给的字面就是入库字面（conn/ctrl/…），单引号
     * 加倍转义后等值比较（LIKE 会把下划线当通配符，等值才对）。 */
    if (category && category[0]) {
        int wrote;
        storeSqlQuote(catLit, (int)sizeof(catLit), category);
        wrote = snprintf(where + whereLen, sizeof(where) - (size_t)whereLen,
                         " AND category = '%s'", catLit);
        if (wrote > 0 && whereLen + wrote < (int)sizeof(where))
            whereLen += wrote;
    }
    /* [关键字下推] 与 0x0118 同口径：时间+类别+关键字共用同一条 WHERE，
     * "总数"与"页内容"永远一致。 */
    if (kw && kw[0]) {
        /* [筛选必须真的下推] 与历史表同款：条件建不出来就不查（返回 0 行
         * + 日志），绝不"当没筛"——那会让用户以为筛选生效了。 */
        if (!storeBuildLogKwCondition(kwCond, (int)sizeof(kwCond), kw)) {
            XPrintf("HsfServerStore: 日志关键字条件未建出（kw=%s）⇒ 本次查询返回 0 行\n",
                    kw);
            return 0;
        }
        if (whereLen < (int)sizeof(where))
            snprintf(where + whereLen, sizeof(where) - (size_t)whereLen, "%s",
                     kwCond);
    }

    /* 窗口内总行数（分页总数按它算）。 */
    snprintf(sql, sizeof(sql), "SELECT COUNT(*) FROM logs%s", where);
    if (storeQueryInt(st, sql, &count) && outTotal)
        *outTotal = (int)count;

    /* 本页行（按 ts 倒序取，再在内存里翻正 ⇒ 时间正序，与 queryHistory 同）。 */
    snprintf(sql, sizeof(sql),
             "SELECT ts,category,text FROM logs%s"
             " ORDER BY ts DESC LIMIT %d OFFSET %d",
             where, limit, offset);
    q = XSqlDatabase_exec_utf8(st->m_db, sql);
    if (!q) return 0;
    if (XSqlQuery_isActive(q) && XSqlQuery_first(q)) {
        do {
            rec = XSqlQuery_record(q);
            if (!rec) break;
            if (written < outMaxRows) {
                HsfLogRow* row = &outRows[written];
                int c;
                row->m_ts = 0;
                row->m_category[0] = '\0';
                row->m_text[0] = '\0';
                /* [必须用 _const 借用版] 与 queryHistory 同款：XSqlRecord_value
                 * 返回堆副本须调用方释放，_const 返回内部地址零分配。 */
                for (c = 0; c < 3; ++c) {
                    char buf[256];
                    const XVariant* var = XSqlRecord_value_const(rec, c);
                    XString* s;
                    buf[0] = '\0';
                    if (!var || !XVariant_isValid(var)) continue;
                    s = XVariant_toString(var);
                    if (s) {
                        const char* utf8 = XString_toUtf8(s);
                        if (utf8) snprintf(buf, sizeof(buf), "%s", utf8);
                        XClassDelete((XClass*)s);
                    }
                    if (c == 0) {
                        /* [大数值不走 atof] epoch 毫秒过 double 会丢精度
                         * （storeCell 同款教训），直接按 int64 取。 */
                        row->m_ts = XVariant_toInt64(var);
                    } else if (c == 1) {
                        snprintf(row->m_category, sizeof(row->m_category),
                                 "%s", buf);
                    } else {
                        snprintf(row->m_text, sizeof(row->m_text), "%s", buf);
                    }
                }
                written++;
            }
            /* [记录副本回收] XSqlQuery_record 返回新记录所有权，逐行释放
             * （queryHistory 同款，不在此重复论证）。 */
            XClassDelete((XClass*)rec);
        } while (XSqlQuery_next(q));
    }
    XClassDelete((XClass*)q);

    /* 倒序取的 ⇒ 反转让输出时间正序（与 queryHistory 同）。 */
    if (written > 1) {
        int i;
        for (i = 0; i < written / 2; ++i) {
            HsfLogRow tmp = outRows[i];
            outRows[i] = outRows[written - 1 - i];
            outRows[written - 1 - i] = tmp;
        }
    }
    return written;
}

/* ==================== 历史分页查询 ==================== */

/* [HSF_STORE_VALID_TS_MIN 已移至文件头部共用区] history/logs 两表查询共用，
 * 定义见顶部（源序在 logs 查询之前）。 */

/** @brief 历史表列名（与 Python store.HISTORY_COLUMNS 同序）。
 *  @note  water_temp 追加在**末尾**（见 HsfServerStore.h：线上按下标发,
 *         插中间会平移既有下标）。 */
static const char* const s_histColumns[HSF_HISTORY_COLUMNS] = {
    "ts", "mode", "vfd_state", "freq_hz", "current_a", "rpm",
    "turb1", "turb2", "flow", "flow_unit", "flow_total", "total_unit",
    "press_mpa", "press_note", "temp", "hum", "di", "do",
    "water_temp"
};

/**
 * @brief 取一格的值文本（NULL → m_isNull=true）。
 * @details 数值列按原样给出（"%.4f" 会把 25.0 变成 25.0000，界面再按
 *          自己的口径格式化会重复）；文本列原样。
 */
static void storeCell(HsfHistoryCell* cell, const XVariant* var, bool numeric)
{
    if (!cell) return;
    cell->m_text[0] = '\0';
    cell->m_isNull = true;
    if (!var || !XVariant_isValid(var)) return;
    if (numeric) {
        const double v = XVariant_toDouble(var);
        /* [不用 %g 记大整数] "%.10g" 会把 epoch 毫秒写成科学计数法
         * （"1.7939e+12"），客户端 strtoll 只取到 "1" ⇒ 时间显示成 1970
         * （实测：真实时间戳变成 1，界面全显示 1970-1-1）。
         * 大数值走 "%.0f"（整数文本），小数才用 %g。 */
        if (v >= 1e9 || (v <= -1e9 && v >= -1e30))
            snprintf(cell->m_text, sizeof(cell->m_text), "%.0f", v);
        else
            snprintf(cell->m_text, sizeof(cell->m_text), "%.10g", v);
    } else {
        XString* s = XVariant_toString(var);
        if (s) {
            const char* utf8 = XString_toUtf8(s);
            if (utf8)
                snprintf(cell->m_text, sizeof(cell->m_text), "%s", utf8);
            /* [恒释放] 原实现把 XClassDelete 放在 if (utf8) 内：
             * XString_toUtf8 返回 NULL（转换失败/空缓存）时 s 泄漏。
             * 副本所有权始终在调用方（XVariant_toString =
             * XString_fromVariant，内部 XString_create_copy）。 */
            XClassDelete((XClass*)s);
        }
    }
    cell->m_isNull = false;
}

/** @brief 该列是不是数值列（决定取值与格式化口径）。 */
static bool storeColIsNumeric(int col)
{
    switch (col) {
    case 0:   /* ts */
    case 3: case 4: case 5: case 6: case 7: case 8:
    case 10: case 12: case 14: case 15: case 16: case 17:
    case 18:  /* water_temp（水温 ℃） */
        return true;
    default:
        return false;
    }
}

/** @brief 执行一条查询并把首行首列读成整数（COUNT/MIN/MAX 用）。 */
static bool storeQueryInt(HsfServerStore* st, const char* sql, int64_t* out)
{
    XSqlQuery* q;
    XSqlRecord* rec;
    bool ok = false;

    if (!st || !st->m_db || !out) return false;
    *out = 0;
    q = XSqlDatabase_exec_utf8(st->m_db, sql);
    if (!q) return false;
    if (XSqlQuery_isActive(q) && XSqlQuery_first(q)) {
        rec = XSqlQuery_record(q);
        if (rec) {
            /* [借用版] 同 queryHistory：XSqlRecord_value 返回堆副本须释放，
             * 这里原先也没释放。HISTORY_PAGE 每次调用会走 4 次本函数
             * （COUNT 窗口/COUNT 全库/MIN/MAX），是固定开销那部分的来源。 */
            const XVariant* v = XSqlRecord_value_const(rec, 0);
            if (v) {
                *out = XVariant_toInt64(v);
                ok = true;
            }
            /* [记录副本回收 2026-11-08] XSqlQuery_record 返回的是**新记录
             * 所有权**（XSqlQuery.h：“调用者使用 XClassDelete 释放”），不是
             * 查询内部借用；原实现只删 q 不删 rec，每次本函数漏一个记录副本。
             * 本函数跑 COUNT(*)/MIN/MAX，记录只有 **1 个字段**，单次约
             * 0.96KB；HISTORY_PAGE 一次调用走 4 次，合计约 3.8KB 固定开销。
             * 注：行记录副本是 18 字段、约 7.5KB，两者不是同一量级，别混算。 */
            XClassDelete((XClass*)rec);
        }
    }
    XClassDelete((XClass*)q);
    /* [实测残余 2026-11-08] 单次查询净增 +40 字节（100 连查逐次一致，
     * 定位在 exec→delq 之间的 SQL 通用层，非本函数漏记录副本）。量级
     * 可忽略（HISTORY_PAGE 4 次/请求 =160B），已记入遗留清单待专项处理。 */
    return ok;
}

/**
 * @brief 显示列表达式（与 Python store.DISPLAY_EXPR 逐条同口径）。
 * @details 只用于**关键字下推**（LIKE 比对的文本口径必须与界面/导出
 *          一致，否则“搜得到”与“看得到”会不一致）。
 */
static const char* const s_histDisplayExpr[HSF_HISTORY_DISPLAY_COLS] = {
    "strftime('%Y-%m-%d %H:%M:%S', ts / 1000.0, 'unixepoch', 'localtime')",
    "COALESCE(mode, '--')",
    "COALESCE(vfd_state, '--')",
    "CASE WHEN freq_hz IS NULL THEN '--' ELSE printf('%.2f', freq_hz) END",
    "CASE WHEN current_a IS NULL THEN '--' ELSE printf('%.2f', current_a) END",
    "CASE WHEN rpm IS NULL THEN '--' ELSE printf('%.0f', rpm) END",
    "CASE WHEN turb1 IS NULL THEN '--' ELSE printf('%.2f', turb1) END",
    "CASE WHEN turb2 IS NULL THEN '--' ELSE printf('%.2f', turb2) END",
    /* [水温 —— 显示列第 8 位，紧跟「浊度2」] 线上列序里它在末尾
     * （HSF_HISTORY_COLUMNS），显示列序由本表决定，两套顺序互不影响。 */
    "CASE WHEN water_temp IS NULL THEN '--' ELSE printf('%.1f', water_temp) END",
    "CASE WHEN flow IS NULL THEN '--' ELSE printf('%.2f', flow) || ' ' || "
    "COALESCE(flow_unit, '') END",
    "CASE WHEN flow_total IS NULL THEN '--' ELSE printf('%.2f', flow_total)"
    " || ' ' || COALESCE(total_unit, '') END",
    "CASE WHEN press_note IS NOT NULL AND press_note != '' THEN press_note "
    "WHEN press_mpa IS NULL THEN '--' ELSE printf('%.3f', press_mpa) END",
    "CASE WHEN temp IS NULL THEN '--' ELSE printf('%.1f', temp) END",
    "CASE WHEN hum IS NULL THEN '--' ELSE printf('%.1f', hum) END",
    "CASE WHEN di IS NULL THEN '--' ELSE printf('0x%08X', di & 0xFFFFFFFF) END",
    "CASE WHEN do IS NULL THEN '--' ELSE printf('0x%08X', do & 0xFFFFFFFF) END",
};

/**
 * @brief 拼关键字条件（与 Python store._keyword_condition 同口径）。
 * @param out 输出 SQL 片段（以 " AND (…)" 开头）。
 * @param cap 输出缓冲容量。
 * @param kw 关键字（utf8；NULL/空=不筛）。
 * @param cols 显示列下标数组（可 NULL/空=全部列）。
 * @param colCount 数组长度。
 * @return 写入条件返回 1；无需筛选返回 0。
 * @details [转义] 关键字当字面量：SQL 单引号加倍，LIKE 的 \ % _ 前加
 *          反斜杠并声明 ESCAPE '\\'——否则用户输入 %/_ 会变成通配符，
 *          搜“0.00”会误命中一大片。
 */
static int storeBuildKwCondition(char* out, int cap, const char* kw,
                                 const uint32_t* cols, int colCount)
{
    char lit[192];
    size_t n = 0;
    int i;
    bool first = true;
    int used = 0;

    if (!out || cap <= 0) return 0;
    out[0] = '\0';
    if (!kw || !kw[0]) return 0;
    {
        /* [与 Python 同口径] 超长只取前 128 字符继续筛（不整条丢弃——
         * 丢条件会把"筛选"变成"看全库"，与 Python 行为不一致）。 */
        size_t kwLen = strlen(kw);
        size_t k;
        if (kwLen > 128) kwLen = 128;
        for (k = 0; k < kwLen && n < sizeof(lit) - 8; ++k) {
            unsigned char ch = (unsigned char)kw[k];
            if (ch == '\'') { lit[n++] = '\''; lit[n++] = '\''; }
            else if (ch == '\\' || ch == '%' || ch == '_') {
                lit[n++] = '\\';
                lit[n++] = (char)ch;
            } else {
                lit[n++] = (char)ch;
            }
        }
        lit[n] = '\0';
    }

    used = snprintf(out, (size_t)cap, " AND (");
    if (used < 0) return 0;
    for (i = 0; i < colCount; ++i) {
        int idx = (int)cols[i];
        int wrote;
        if (idx < 0 || idx >= HSF_HISTORY_DISPLAY_COLS) continue;
        wrote = snprintf(out + used, (size_t)(cap - used),
                         "%sCOALESCE(%s, '')",
                         first ? "" : " || ' ' || ", s_histDisplayExpr[idx]);
        if (wrote < 0 || used + wrote >= cap) {
            /* [半截 SQL 绝不能交出去] 旧实现直接 return 0，out 里却留着
             * 拼到一半的 " AND (…" ⇒ 调用方只判 out[0] 非空就把它拼进
             * WHERE ⇒ SQL 语法错 ⇒ 查询报不出错、只是**返回 0 行** ⇒
             * 用户看到"关键字筛选毫无反应"。2026-11-10 实锅：16 列时条件
             * 1544 字节 vs 旧缓冲 1536，差 8 字节就踩中。缓冲已放大，
             * 这里再兜一道：溢出即清空并置"未建出条件"标记，由调用方
             * 决定是失败还是放行，绝不拿半截 SQL 去查。 */
            out[0] = '\0';
            return 0;
        }
        used += wrote;
        first = false;
    }
    if (first) {
        /* 下标全越界：退回全部列（勿把条件拼成空白）。 */
        for (i = 0; i < HSF_HISTORY_DISPLAY_COLS; ++i) {
            int wrote = snprintf(out + used, (size_t)(cap - used),
                                 "%sCOALESCE(%s, '')",
                                 first ? "" : " || ' ' || ",
                                 s_histDisplayExpr[i]);
            if (wrote < 0 || used + wrote >= cap) return 0;
            used += wrote;
            first = false;
        }
    }
    if (snprintf(out + used, (size_t)(cap - used),
                 ") LIKE '%%%s%%' ESCAPE '\\'", lit) < 0)
        return 0;
    return 1;
}

int HsfServerStore_queryHistory(HsfServerStore* st, int offset, int limit,
                                int64_t loMs, int64_t hiMs,
                                const char* kw,
                                const uint32_t* kwCols, int kwColCount,
                                HsfHistoryQueryRow* outRows, int outMaxRows,
                                int* outWindowTotal, int64_t* outDbRows,
                                int64_t* outLatestTs, int64_t* outEarliestTs)
{
    /* [缓冲尺寸跟显示列数走] 关键字条件是「全部显示列的文本表达式」拼成的
     * 一条长串：16 列实测 1544 字节（旧的 1536 缓冲**刚好差 8 字节**溢出
     * ⇒ 拼出半截 SQL ⇒ 查询报语法错 ⇒ 关键字筛选静默失效、返回 0 行）。
     * 加列只会让这条串更长，故缓冲按**最坏情况**给足（每列 ~120 字节 ×
     * 32 列余量），不再按当前列数卡尺寸。 */
    char sql[8192];
    char where[8192];
    char kwCond[8192];
    XSqlQuery* q;
    XSqlRecord* rec;
    int written = 0;
    int64_t count = 0;
    int64_t v = 0;
    int whereLen;

    if (outWindowTotal) *outWindowTotal = 0;
    if (outDbRows) *outDbRows = 0;
    if (outLatestTs) *outLatestTs = 0;
    if (outEarliestTs) *outEarliestTs = 0;
    if (!st || !st->m_db || !outRows || outMaxRows <= 0) return 0;
    if (offset < 0) offset = 0;
    if (limit <= 0) limit = 1;

    /* [时间窗] 只认有效墙钟（过滤掉旧版本误写的 monotonic 行）。 */
    if (loMs > 0 && hiMs > 0) {
        snprintf(where, sizeof(where),
                 " WHERE ts >= %lld AND ts <= %lld AND ts >= %lld",
                 (long long)loMs, (long long)hiMs, (long long)HSF_STORE_VALID_TS_MIN);
    } else if (loMs > 0) {
        snprintf(where, sizeof(where), " WHERE ts >= %lld AND ts >= %lld",
                 (long long)loMs, (long long)HSF_STORE_VALID_TS_MIN);
    } else if (hiMs > 0) {
        snprintf(where, sizeof(where), " WHERE ts <= %lld AND ts >= %lld",
                 (long long)hiMs, (long long)HSF_STORE_VALID_TS_MIN);
    } else {
        snprintf(where, sizeof(where), " WHERE ts >= %lld",
                 (long long)HSF_STORE_VALID_TS_MIN);
    }
    /* [关键字下推] 与 Python 同口径：时间 + 关键字共用同一条 WHERE，
     * 保证"总数"与"页内容"永远一致。 */
    whereLen = (int)strlen(where);
    if (kw && kw[0]) {
        /* [筛选必须真的下推] 条件建不出来（缓冲不够等）就不能"当没筛"——
         * 否则用户看到的是"筛了等于没筛"。宁可 0 行 + 一条明确日志，
         * 也不把半截 SQL / 空条件交给查询。 */
        if (!storeBuildKwCondition(kwCond, (int)sizeof(kwCond), kw, kwCols,
                                   kwColCount)) {
            XPrintf("HsfServerStore: 关键字条件未建出（kw=%s）⇒ 本次查询返回 0 行\n",
                    kw);
            return 0;
        }
        if (whereLen < (int)sizeof(where))
            snprintf(where + whereLen, sizeof(where) - (size_t)whereLen, "%s",
                     kwCond);
    }

    /* 窗口内总行数（页码总数按它算，不是全库行数）。 */
    snprintf(sql, sizeof(sql), "SELECT COUNT(*) FROM history%s", where);
    if (storeQueryInt(st, sql, &count) && outWindowTotal)
        *outWindowTotal = (int)count;

    /* 全库行数 / 最早 / 最新（同样只认有效墙钟）。 */
    snprintf(sql, sizeof(sql),
             "SELECT COUNT(*) FROM history WHERE ts >= %lld",
             (long long)HSF_STORE_VALID_TS_MIN);
    if (storeQueryInt(st, sql, &v) && outDbRows) *outDbRows = v;
    snprintf(sql, sizeof(sql),
             "SELECT MIN(ts) FROM history WHERE ts >= %lld",
             (long long)HSF_STORE_VALID_TS_MIN);
    if (storeQueryInt(st, sql, &v) && outEarliestTs) *outEarliestTs = v;
    snprintf(sql, sizeof(sql),
             "SELECT MAX(ts) FROM history WHERE ts >= %lld",
             (long long)HSF_STORE_VALID_TS_MIN);
    if (storeQueryInt(st, sql, &v) && outLatestTs) *outLatestTs = v;

    /* 本页行（按 ts 倒序取，再在内存里翻正 ⇒ 时间正序，与 Python 同）。 */
    snprintf(sql, sizeof(sql),
             "SELECT ts,mode,vfd_state,freq_hz,current_a,rpm,turb1,turb2,"
             "flow,flow_unit,flow_total,total_unit,press_mpa,press_note,"
             "temp,hum,di,do,water_temp FROM history%s"
             " ORDER BY ts DESC LIMIT %d OFFSET %d",
             where, limit, offset);
    q = XSqlDatabase_exec_utf8(st->m_db, sql);
    if (!q) return 0;
    if (XSqlQuery_isActive(q) && XSqlQuery_first(q)) {
        do {
            rec = XSqlQuery_record(q);
            if (!rec) break;
            if (written < outMaxRows) {
                HsfHistoryQueryRow* row = &outRows[written];
                int c;
                for (c = 0; c < HSF_HISTORY_COLUMNS; ++c) {
                    /* [必须用 _const 借用版] XSqlRecord_value 走
                     * XSqlField_value → XVariant_create_copy，返回的是**堆
                     * 副本、所有权归调用方**。原实现在这里每格取一次副本却
                     * 从不释放。实测（记录副本 + 单格副本合计）：limit=20 时
                     * 每次 HISTORY_PAGE +182,648 字节、limit=1 时 +14,360；
                     * 本条 _const 修复后分别降到 +153,688 / +11,328，余下
                     * 即下方 XSqlRecord 副本（每行 1 个，约 7,493 字节/行）。
                     * _const 返回内部字段地址，零分配、禁止释放。 */
                    const XVariant* var = XSqlRecord_value_const(rec, c);
                    storeCell(&row->m_cells[c], var, storeColIsNumeric(c));
                }
                written++;
            }
            /* [记录副本回收 2026-11-08] XSqlQuery_record 每次都返回**新记录
             * 所有权**（XSqlQuery.h：“调用者使用 XClassDelete 释放”），不是
             * 查询内部借用。原实现只在循环外删 q、从不删 rec ⇒ 每行漏一个
             * 记录副本（18 字段，≈7,493 字节）。limit=20 时一次
             * HISTORY_PAGE 的实际构成：20 行 × 7,493 + 4 次 COUNT/MIN/MAX
             * 的单字段副本（各约 959）= 约 153.7KB，这是 C 客户端每轮增长
             * 的主项。注意 `if (!rec) break;` 分支 rec 为 NULL 不需释放，
             * 其余路径（含 written 已满）都走到本行统一释放。 */
            XClassDelete((XClass*)rec);
        } while (XSqlQuery_next(q));
    }
    XClassDelete((XClass*)q);

    /* 倒序取的 ⇒ 反转让输出时间正序（Python 侧同样 reversed(rows)）。 */
    if (written > 1) {
        int i;
        for (i = 0; i < written / 2; ++i) {
            HsfHistoryQueryRow tmp = outRows[i];
            outRows[i] = outRows[written - 1 - i];
            outRows[written - 1 - i] = tmp;
        }
    }
    return written;
}

#endif /* XSQL_ON */
