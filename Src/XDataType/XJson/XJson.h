#ifndef XJSON_H
#define XJSON_H

#ifdef __cplusplus
extern "C" {
#endif
#include "CXinYueConfig.h"
#include "XTypes.h"
typedef enum XJsonDocumentFormat
{
/*     {
         "Array": [
             true,
             999,
             "string"
         ],
         "Key": "Value",
         "null": null
     }
*/
    XJsonDocument_Indented,
// { "Array": [true,999,"string"] ,"Key" : "Value","null" : null }
    XJsonDocument_Compact
} XJsonDocumentFormat;
#ifdef __cplusplus
}
#endif

#endif // XJSONARRAY_H