/**
 * @file       XMySqlDriver.h
 * @brief      MySQL/MariaDB æºç é©±å¨ã
 * @details    é©±å¨éè¿ XSqlMySqlClientApi è®¿é®å®¢æ·ç«¯å®ç°ï¼å¬å±å¤´æä»¶ä¸
 *             æ´é²ç¬¬ä¸æ¹å®¢æ·ç«¯ç±»åï¼é»è®¤å®¢æ·ç«¯ä½¿ç¨ XinYueC ç½ç»æ½è±¡ã
 */
#ifndef XMYSQLDRIVER_H
#define XMYSQLDRIVER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "XSqlDriver.h"
#include "XSqlMySqlClient.h"

/**
 * @brief åå»º MySQL/MariaDB æºç é©±å¨ã
 * @return æ°é©±å¨å¯¹è±¡ï¼è°ç¨èåå¾æææå¹¶å¿é¡»ä½¿ç¨ XSqlDriver_delete_base éæ¾ï¼åå­ä¸è¶³æé»è®¤å®¢æ·ç«¯ä¸å¯ç¨æ¶è¿å NULLã
 */
XSqlDriver* XMySqlDriver_create(void);

/**
 * @brief æ³¨å MySQL åç½®æºç é©±å¨ã
 * @return æ³¨åæåè¿å trueï¼åå­ä¸è¶³ææ³¨åå¤±è´¥è¿å falseã
 * @note æ³¨åè¡¨æ¥ç®¡åå»ºå¨ï¼è¯¥å½æ°å¯éå¤è°ç¨ã
 */
bool XMySqlDriver_register(void);

/**
 * @brief è®¾ç½® MySQL å®¢æ·ç«¯å®ç°å½æ°è¡¨ã
 * @param api å®¢æ·ç«¯å½æ°è¡¨ï¼åç¨ï¼å¿é¡»åå«é©±å¨å®éä½¿ç¨çå¨é¨å½æ°ï¼ä¼ å¥ NULL æ¢å¤é»è®¤å®ç°ã
 * @return è®¾ç½®æåè¿å trueï¼åæ°ä¸å®æ´è¿å falseã
 * @note åºå¨åå»º MySQL è¿æ¥åè°ç¨ï¼å·²æè¿æ¥ä»ä½¿ç¨åå»ºæ¶çå½æ°è¡¨ã
 */
bool XMySqlDriver_setClientApi(const XSqlMySqlClientApi* api);

/**
 * @brief è·åå½å MySQL å®¢æ·ç«¯å®ç°å½æ°è¡¨ã
 * @return è¿ç¨åå±äº«å½æ°è¡¨ï¼è°ç¨æ¹åªå¯è¯»åï¼ä¸å¾éæ¾æä¿®æ¹ã
 */
const XSqlMySqlClientApi* XMySqlDriver_clientApi(void);

#ifdef __cplusplus
}
#endif

#endif /* XMYSQLDRIVER_H */
