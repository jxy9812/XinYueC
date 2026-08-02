/**
 * @file       XSqlMySqlTest.h
 * @brief      MySQL/MariaDB çå®æå¡å¨èè°æµè¯å£°æã
 */
#ifndef XSQLMYSQLTEST_H
#define XSQLMYSQLTEST_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief æ§è¡ MySQL/MariaDB å¨æµç¨èè°æµè¯ã
 * @return æµè¯éè¿ææªéç½®æå¡å¨æ¶è¿å trueï¼éç½®åä»»ä¸æä½å¤±è´¥è¿å falseã
 * @note è¿æ¥åæ°ä» XMYSQL_TEST_HOSTãXMYSQL_TEST_PORTãXMYSQL_TEST_DATABASEã
 *       XMYSQL_TEST_USERãXMYSQL_TEST_PASSWORD å XMYSQL_TEST_TABLE è¯»åï¼
 *       æªè®¾ç½® XMYSQL_TEST_DATABASE æ¶èªå¨ä½¿ç¨ä¸´æ¶æµè¯åºã
 * @warning å½æ°å¯è½åå»ºãä¿®æ¹åå é¤æµè¯æ°æ®åºææµè¯è¡¨ï¼åªè½æåä¸ç¨æµè¯å®ä¾ã
 */
bool XSqlMySqlTest_run(void);

#ifdef __cplusplus
}
#endif

#endif /* XSQLMYSQLTEST_H */
