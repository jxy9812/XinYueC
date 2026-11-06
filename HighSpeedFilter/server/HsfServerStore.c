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
                  " di INTEGER, do INTEGER)");
        storeExec(st, "CREATE INDEX IF NOT EXISTS idx_history_ts ON history(ts)");
        storeExec(st,
                  "CREATE TABLE IF NOT EXISTS alarms ("
                  " ts INTEGER, kind TEXT, source TEXT, text TEXT)");
        storeExec(st, "CREATE INDEX IF NOT EXISTS idx_alarms_ts ON alarms(ts)");
        if (!st->m_openLogged) {
            st->m_openLogged = true;
            XPrintf("HsfServerStore: 落盘已启用（%s，采样 %dms，"
                    "history+alarms 两表）\n",
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

    snprintf(sql, sizeof(sql),
             "INSERT OR REPLACE INTO history(ts,mode,vfd_state,freq_hz,"
             "current_a,rpm,turb1,turb2,flow,flow_unit,flow_total,"
             "total_unit,press_mpa,press_note,temp,hum,di,do) VALUES("
             "%lld,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%u,%u)",
             (long long)row->tsMs, cMode, cVfd, cFreq, cCur, cRpm, cT1, cT2,
             cFlow, cFU, cTotal, cTU, cPress, cPN, cTemp, cHum,
             row->diValid ? (unsigned)row->di : 0u,
             row->doValid ? (unsigned)row->doMask : 0u);
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

/* ==================== 历史分页查询 ==================== */

/** @brief 合法墙钟毫秒下界（2020-01-01；与 Python VALID_TS_MIN 同值）。 */
#define HSF_STORE_VALID_TS_MIN  1577836800000LL

/** @brief 历史表列名（与 Python store.HISTORY_COLUMNS 同序）。 */
static const char* const s_histColumns[HSF_HISTORY_COLUMNS] = {
    "ts", "mode", "vfd_state", "freq_hz", "current_a", "rpm",
    "turb1", "turb2", "flow", "flow_unit", "flow_total", "total_unit",
    "press_mpa", "press_note", "temp", "hum", "di", "do"
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
            if (utf8) {
                snprintf(cell->m_text, sizeof(cell->m_text), "%s", utf8);
                XClassDelete((XClass*)s);
            }
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
    XVariant* v;
    bool ok = false;

    if (!st || !st->m_db || !out) return false;
    *out = 0;
    q = XSqlDatabase_exec_utf8(st->m_db, sql);
    if (!q) return false;
    if (XSqlQuery_isActive(q) && XSqlQuery_first(q)) {
        rec = XSqlQuery_record(q);
        if (rec) {
            v = XSqlRecord_value(rec, 0);
            if (v) {
                *out = XVariant_toInt64(v);
                ok = true;
            }
        }
    }
    XClassDelete((XClass*)q);
    return ok;
}

int HsfServerStore_queryHistory(HsfServerStore* st, int offset, int limit,
                                int64_t loMs, int64_t hiMs,
                                HsfHistoryQueryRow* outRows, int outMaxRows,
                                int* outWindowTotal, int64_t* outDbRows,
                                int64_t* outLatestTs, int64_t* outEarliestTs)
{
    char sql[1024];
    char where[192];
    XSqlQuery* q;
    XSqlRecord* rec;
    int written = 0;
    int64_t count = 0;
    int64_t v = 0;

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
             "temp,hum,di,do FROM history%s"
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
                    XVariant* var = XSqlRecord_value(rec, c);
                    storeCell(&row->m_cells[c], var, storeColIsNumeric(c));
                }
                written++;
            }
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
