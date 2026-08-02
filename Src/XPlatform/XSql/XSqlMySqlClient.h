/**
 * @file       XSqlMySqlClient.h
 * @brief      MySQL å®¢æ·ç«¯æºç ééæ¥å£ã
 * @details    æ¬æä»¶åªå®ä¹ MySQL åè®®å®¢æ·ç«¯ä¸ XSql é©±å¨ä¹é´çæ½è±¡è¾¹çï¼
 *             ä¸åå« mysql.hãMariaDB å¤´æä»¶æä»»ä½å¹³å° APIãé»è®¤å®ç°ä½äº
 *             Src/XCode/XSqlï¼å¹¶éè¿ XinYueC çç½ç»ååå­æ¥å£å·¥ä½ã
 */
#ifndef XSQLMYSQLCLIENT_H
#define XSQLMYSQLCLIENT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "XSqlGlobal.h"

/** @brief MySQL å®¢æ·ç«¯è¿æ¥çä¸éæå®ç°ç±»åï¼åªè½ç±å®¢æ·ç«¯å½æ°è¡¨åå»ºåéæ¯ã */
typedef struct XSqlMySqlClient XSqlMySqlClient;
/** @brief MySQL æ¥è¯¢ç»æçä¸éæå®ç°ç±»åï¼åªè½ç±å®¢æ·ç«¯å½æ°è¡¨éæ¯ã */
typedef struct XSqlMySqlResult XSqlMySqlResult;

/**
 * @brief MySQL å­æ®µåå¼çéç¨ç±»åã
 */
typedef enum XSqlMySqlValueType {
    XSqlMySqlValueType_Unknown = 0,  /**< æªç¥ç±»åã */
    XSqlMySqlValueType_Null,         /**< NULLã */
    XSqlMySqlValueType_Integer,      /**< æç¬¦å·æ´æ°ã */
    XSqlMySqlValueType_UnsignedInteger, /**< æ ç¬¦å·æ´æ°ã */
    XSqlMySqlValueType_Real,         /**< æµ®ç¹æ°æå®ç¹æ°ã */
    XSqlMySqlValueType_String,       /**< ææ¬å­ç¬¦ä¸²ã */
    XSqlMySqlValueType_ByteArray,    /**< äºè¿å¶æ°æ®ã */
    XSqlMySqlValueType_Date,         /**< æ¥æå¼ã */
    XSqlMySqlValueType_Time,         /**< æ¶é´å¼ã */
    XSqlMySqlValueType_DateTime      /**< æ¥ææ¶é´å¼ã */
} XSqlMySqlValueType;

/**
 * @brief MySQL äºè¿å¶é¢å¤çåæ°çæ æææè§å¾ã
 * @note m_data å¨ executePrepared è¿ååå¿é¡»ä¿æææï¼å®¢æ·ç«¯ä¸ä¼ä¿å­æéã
 */
typedef struct XSqlMySqlBind {
    XSqlMySqlValueType m_type; /**< åæ°å¼ç±»åã */
    const void* m_data;        /**< åæ°æ°æ®åç¨æéï¼é NULL å¼å¿é¡»å¨è°ç¨ç»æåææã */
    size_t m_size;             /**< åæ°æ°æ®å­èæ°ï¼ææ¬ä¸è¦æ±åå«æ«å°¾ NULã */
    bool m_isNull;             /**< ä¸º true æ¶å¿½ç¥ m_data å m_size å¹¶ç»å® SQL NULLã */
    bool m_unsigned;           /**< æ´æ°åæ°æ¯å¦ææ ç¬¦å·å¼ç¼ç ã */
} XSqlMySqlBind;

/**
 * @brief MySQL å­æ®µåæ°æ®è§å¾ã
 * @note ææå­ç¬¦ä¸²åç±ç»æå¯¹è±¡åç¨ï¼ç»æå¯¹è±¡éæ¯åå¤±æã
 */
typedef struct XSqlMySqlField {
    const char* m_name;          /**< å­æ®µåç§°ï¼UTF-8ã */
    const char* m_table;         /**< æå±è¡¨åç§°ï¼UTF-8ã */
    const char* m_database;      /**< æå±æ°æ®åºåç§°ï¼UTF-8ã */
    XSqlMySqlValueType m_type;   /**< å­æ®µç±»åã */
    uint32_t m_length;           /**< æå¡ç«¯å£°æçå­æ®µé¿åº¦ã */
    uint32_t m_flags;            /**< MySQL å­æ®µæ å¿ã */
    uint8_t m_nativeType;        /**< MySQL åçå­æ®µç±»åä»£ç ã */
    uint8_t m_decimals;          /**< å®ç¹/æµ®ç¹å­æ®µçå°æ°ä½æ°ã */
    bool m_unsigned;             /**< æ¯å¦ä¸ºæ ç¬¦å·æ°ã */
} XSqlMySqlField;

/**
 * @brief MySQL ç»æå¼è§å¾ã
 * @note m_data ç±ç»æå¯¹è±¡ææï¼è°ç¨èåªè½å¨å½åç»æå¯¹è±¡æææé´è¯»åã
 */
typedef struct XSqlMySqlValue {
    const void* m_data;          /**< åå§å¼æ°æ®ï¼ä¸ä¿è¯ä»¥ NULL ç»å°¾ã */
    size_t m_size;               /**< åå§å¼é¿åº¦ã */
    XSqlMySqlValueType m_type;   /**< å¼ç±»åã */
    bool m_isNull;               /**< æ¯å¦ä¸º NULLã */
} XSqlMySqlValue;

/**
 * @brief MySQL å®¢æ·ç«¯éè¯¯è§å¾ã
 * @note å­ç¬¦ä¸²ç±å®¢æ·ç«¯å¯¹è±¡ææï¼ä¸ä¸æ¬¡æä½æå¯¹è±¡éæ¯åå¤±æã
 */
typedef struct XSqlMySqlError {
    const char* m_driverText;    /**< é©±å¨éè¯¯ææ¬ã */
    const char* m_databaseText;  /**< æå¡ç«¯éè¯¯ææ¬ã */
    const char* m_errorCode;     /**< æå¡ç«¯éè¯¯ç ææ¬ã */
    XSqlErrorType m_type;        /**< XinYueC SQL éè¯¯ç±»åã */
} XSqlMySqlError;

/**
 * @brief å¯æ¿æ¢ç MySQL å®¢æ·ç«¯å½æ°è¡¨ã
 * @details XMySqlDriver åªä¾èµæ¬å½æ°è¡¨ãç§»æ¤å°åµå¥å¼æ¶å¯ä»¥ä¿çé»è®¤ç
 *          XinYueC åè®®å®ç°ï¼ä¹å¯ä»¥æä¾åºäºååå®¢æ·ç«¯æºç çå¦ä¸ä»½å½æ°è¡¨ã
 */
typedef struct XSqlMySqlClientApi {
    /** @brief åå»ºæªè¿æ¥ç MySQL å®¢æ·ç«¯ã @return æ°å®¢æ·ç«¯æææï¼å¤±è´¥è¿å NULLã */
    XSqlMySqlClient* (*create)(void);
    /** @brief éæ¯å®¢æ·ç«¯åå¶è¿æ¥ã @param client å®¢æ·ç«¯æææï¼å¯ä¸º NULLã */
    void (*destroy)(XSqlMySqlClient* client);
    /**
     * @brief æå¼ MySQL è¿æ¥ã
     * @param client å®¢æ·ç«¯ï¼ä¸è½ä¸º NULLï¼ç±è°ç¨æ¹åç¨ã
     * @param database é»è®¤æ°æ®åºåï¼UTF-8 åç¨å­ç¬¦ä¸²ï¼å¯ä¸º NULLã
     * @param user ç¨æ·åï¼UTF-8 åç¨å­ç¬¦ä¸²ï¼å¯ä¸º NULLã
     * @param password å¯ç ï¼UTF-8 åç¨å­ç¬¦ä¸²ï¼å¯ä¸º NULLï¼ä¸ç±å®¢æ·ç«¯ä¿å­ã
     * @param host ä¸»æºãUnix å¥æ¥å­ææ¬å°ä¼ è¾ç«¯ç¹ï¼UTF-8 åç¨å­ç¬¦ä¸²ï¼å¯ä¸º NULLã
     * @param port TCP ç«¯å£ï¼å°äº 0 æ¶ç±å®¢æ·ç«¯ä½¿ç¨é»è®¤å¼ã
     * @param options åå·åéè¿æ¥éé¡¹ï¼UTF-8 åç¨å­ç¬¦ä¸²ï¼å¯ä¸º NULLã
     * @return æåè¿å trueï¼å¤±è´¥æ¶å®¢æ·ç«¯è®°å½å¯ç± lastError åå¾çéè¯¯ã
     */
    bool (*open)(XSqlMySqlClient* client, const char* database, const char* user,
                 const char* password, const char* host, int port, const char* options);
    /**
     * @brief å³é­å½åè¿æ¥ã
     * @param client å®¢æ·ç«¯ï¼å¯ä¸º NULLï¼ç±è°ç¨æ¹åç¨ã
     * @return æ ï¼å³é­åå·²æç»æç±ç»æå¯¹è±¡ç»§ç»­ç®¡çï¼å®¢æ·ç«¯ä¸åå¯æ§è¡ SQLã
     */
    void (*close)(XSqlMySqlClient* client);
    /**
     * @brief æ§è¡ UTF-8 SQL ææ¬ã
     * @param client å·²è¿æ¥å®¢æ·ç«¯ï¼ä¸è½ä¸º NULLï¼ç±è°ç¨æ¹åç¨ã
     * @param query SQL å­èåºåï¼åç¨ï¼é¿åº¦ç± length æå®ï¼ä¸è¦æ± NUL ç»å°¾ã
     * @param length query çå­èæ°ã
     * @param result è¾åºç»æï¼æåæ¶åå¥æ°ç»ææææï¼æ ç»æéæ¶ä»å¯ä¸º NULLã
     * @return æå¡ç«¯æ¥åå¹¶å®æè¯·æ±è¿å trueï¼å¤±è´¥æ¶ result å¿é¡»ä¿æ NULLã
     */
    bool (*execute)(XSqlMySqlClient* client, const char* query, size_t length,
                    XSqlMySqlResult** result);
    /**
     * @brief ä»¥æå¡ç«¯äºè¿å¶é¢å¤çåè®®æ§è¡ SQLã
     * @param client å·²è¿æ¥å®¢æ·ç«¯ï¼ä¸è½ä¸º NULLï¼ç±è°ç¨æ¹åç¨ã
     * @param query SQL å­èåºåï¼åç¨ï¼é¿åº¦ç± length æå®ã
     * @param length query çå­èæ°ã
     * @param binds åæ°æ°ç»ï¼åç¨ï¼bindCount ä¸º 0 æ¶å¯ä¸º NULLã
     * @param bindCount binds ä¸­çåæ°ä¸ªæ°ã
     * @param result è¾åºç»æï¼æåæ¶åå¥æ°ç»ææææï¼æ ç»æéæ¶ä»å¯ä¸º NULLã
     * @return æåè¿å trueï¼æªæ¯æææ§è¡å¤±è´¥è¿å falseã
     */
    bool (*executePrepared)(XSqlMySqlClient* client, const char* query, size_t length,
                            const XSqlMySqlBind* binds, size_t bindCount,
                            XSqlMySqlResult** result);
    /** @brief éæ¯æ¥è¯¢ç»æã @param result ç»ææææï¼å¯ä¸º NULLã */
    void (*resultDestroy)(XSqlMySqlResult* result);
    /** @brief è¿åç»æåæ°ã @param result ç»æï¼å¯ä¸º NULLã @return åæ°ï¼æ ç»ææ NULL è¿å 0ã */
    int (*resultColumnCount)(const XSqlMySqlResult* result);
    /** @brief è·åå­æ®µåæ°æ®åç¨è§å¾ã @param result ç»æï¼ä¸è½ä¸º NULLã @param index åç´¢å¼ï¼ä» 0 å¼å§ã @return å­æ®µåç¨è§å¾ï¼è¶çè¿å NULLã */
    const XSqlMySqlField* (*resultField)(const XSqlMySqlResult* result, int index);
    /** @brief å®ä½ç»æè¡ã @param result ç»æï¼ä¸è½ä¸º NULLã @param index è¡ç´¢å¼ï¼ä» 0 å¼å§ã @return å®ä½æåè¿å trueï¼è¶çæå¤±è´¥è¿å falseã */
    bool (*resultFetch)(XSqlMySqlResult* result, int index);
    /** @brief è·åå½åè¡åå¼åç¨è§å¾ã @param result å·²å®ä½ç»æï¼ä¸è½ä¸º NULLã @param index åç´¢å¼ï¼ä» 0 å¼å§ã @return å¼åç¨è§å¾ï¼æ å½åè¡æè¶çè¿å NULLã */
    const XSqlMySqlValue* (*resultValue)(const XSqlMySqlResult* result, int index);
    /** @brief è¿åå¯ç¥çç»æè¡æ°ã @param result ç»æï¼å¯ä¸º NULLã @return è¡æ°ï¼æªç¥æ¶è¿å -1ã */
    int (*resultSize)(const XSqlMySqlResult* result);
    /** @brief è¿åæè¿è¯­å¥çåå½±åè¡æ°ã @param result ç»æï¼å¯ä¸º NULLã @return åå½±åè¡æ°ï¼æªç¥æ¶è¿å -1ã */
    int64_t (*resultRowsAffected)(const XSqlMySqlResult* result);
    /** @brief è¿åæè¿æå¥çèªå¢ IDã @param result ç»æï¼å¯ä¸º NULLã @return èªå¢ IDï¼ä¸å¯ç¨æ¶è¿å 0ã */
    uint64_t (*resultLastInsertId)(const XSqlMySqlResult* result);
    /** @brief å¤æ­æ¯å¦ä¸ºæåç SELECT ç»æã @param result ç»æï¼å¯ä¸º NULLã @return æ¯ SELECT è¿å trueï¼å¦åè¿å falseã */
    bool (*resultIsSelect)(const XSqlMySqlResult* result);
    /** @brief åæ¢å°ä¸ä¸æå¡ç«¯ç»æã @param result å½åç»æï¼ä¸è½ä¸º NULLã @param next è¾åºä¸ä¸ç»ææææï¼æ ä¸ä¸ç»ææ¶ä¿æ NULLã @return å·²åæ¢è¿å trueï¼å¦åè¿å falseã */
    bool (*resultNext)(XSqlMySqlResult* result, XSqlMySqlResult** next);
    /** @brief è·åæè¿å®¢æ·ç«¯éè¯¯åç¨è§å¾ã @param client å®¢æ·ç«¯ï¼å¯ä¸º NULLã @return éè¯¯åç¨è§å¾ï¼æ éè¯¯ä¿¡æ¯æ¶è¿å NULLã */
    const XSqlMySqlError* (*lastError)(const XSqlMySqlClient* client);
    /** @brief è·ååç«¯åçå¥æã @param client å®¢æ·ç«¯ï¼å¯ä¸º NULLã @return åç¨å¥æï¼è°ç¨æ¹ä¸å¾éæ¾ï¼æªå®ç°æ¶è¿å NULLã */
    void* (*handle)(const XSqlMySqlClient* client);
    /** @brief è¯·æ±åæ¶å½åæ¥è¯¢ã @param client å®¢æ·ç«¯ï¼ä¸è½ä¸º NULLã @return å·²åæ¶è¿å trueï¼æªå®ç°ææ æ³åæ¶è¿å falseã */
    bool (*cancel)(XSqlMySqlClient* client);
    /**
     * @brief æ¥è¯¢å½åè¿æ¥æ¯å¦æ¯æäºå¡ã
     * @param client å®¢æ·ç«¯å¯¹è±¡ï¼ç±é©±å¨åç¨ã
     * @return æ¯æè¿å trueï¼ä¸æ¯æè¿å falseã
     * @note å¯éåè°ï¼NULL æ¶é©±å¨æè¿æ¥å·²æå¼å¤çï¼ä»¥å¼å®¹æ§ééå¨ã
     */
    bool (*supportsTransactions)(const XSqlMySqlClient* client);
    /**
     * @brief æ¥è¯¢å½åè¿æ¥æ¯å¦æ¯ææå¡ç«¯é¢å¤çã
     * @param client å®¢æ·ç«¯å¯¹è±¡ï¼ç±é©±å¨åç¨ã
     * @return æ¯æè¿å trueï¼ä¸æ¯æè¿å falseã
     * @note å¯éåè°ï¼NULL æ¶é©±å¨æè¿æ¥å·²æå¼å¤çï¼ä»¥å¼å®¹æ§ééå¨ã
     */
    bool (*supportsPreparedQueries)(const XSqlMySqlClient* client);
} XSqlMySqlClientApi;

/**
 * @brief è·åé»è®¤ MySQL å®¢æ·ç«¯å®ç°ã
 * @return è¿ç¨åå±äº«å½æ°è¡¨ï¼ä¸å¾éæ¾ãæ²¡æå¯ç¨å®ç°æ¶è¿å NULLã
 */
const XSqlMySqlClientApi* XSqlMySqlClient_defaultApi(void);

#ifdef __cplusplus
}
#endif

#endif /* XSQLMYSQLCLIENT_H */
