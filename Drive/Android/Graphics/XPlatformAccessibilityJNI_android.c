/**
 * @file       XPlatformAccessibilityJNI_android.c
 * @brief      安卓无障碍桥的 JNI 实现（供 XGuiA11yBridge 调用）。
 * @details    [2026-10-24 安卓无障碍接入] Java 侧 XGuiA11yBridge 的五个
 *             native 方法在此实现：
 *               - nativeNodeInfo：取渲染线程发布的控件树快照节点，组装
 *                 字符串数组交给 Java 组装 AccessibilityNodeInfo；
 *               - nativePerformClick / nativePerformFocus：读屏器动作
 *                 回投原生——**必须回到渲染线程**执行（控件树线程约束），
 *                 故此处只投递请求，由渲染线程的事件泵消费；
 *               - nativeNodeCount / nativeRootIndex：诊断与根节点查询。
 *
 *             **注册方式用 JNI_OnLoad + RegisterNatives**（非按名查找）：
 *             链接参数含 `-Wl,--gc-sections`，未被 .so 内部引用的
 *             `Java_com_xinyuec_...` 命名符号会被裁掉，ART 按名查找即
 *             "No implementation found"（真机实证崩溃）。RegisterNatives
 *             由本库主动把 C 函数绑到 Java 方法名，不依赖动态符号表。
 *
 *             快照读取加锁（Java/UI 线程读、渲染线程写），只读已发布副本，
 *             不触碰控件树本身。
 * @author     XinYueC 团队
 ******************************************************************************/
#include <string.h>
#include <stdlib.h>

#if XWINDOW_ON && XACCESSIBLE_ON && defined(__ANDROID__)

/* jni.h 只在安卓 NDK 工具链存在：必须放进平台门内，否则 Windows 桌面
 * 统一构建树编译本文件时 C1083（jni.h 不存在）整树阻塞。 */
#include <jni.h>
#include "XPlatformAccessibility.h"
#include "XWidget.h"
#include "XAbstractButton.h"
#include "XVector.h"
#include "XString.h"
#include "XAndroidA11yNodes.h"
#include "XAndroidJavaPackage.h" /* Java 包名唯一来源（FindClass 拼全名用） */

/* 快照接口（XPlatformAccessibility_android.c 实现）。 */
int XAndroid_a11yNodeCount(void);
bool XAndroid_a11yNodeInfo(int index, XAndroidA11yNode* out);
int XAndroid_a11yRootIndex(void);

/* 点击/聚焦请求投递（由渲染线程消费）。 */
bool XAndroid_a11yPostAction(int index, int action);

/* Java 侧的类/方法名（RegisterNatives 用）。 */
#define XA11Y_BRIDGE_CLASS XANDROID_JAVA_JNI_CLASS("XGuiA11yBridge")

static jobjectArray xa11y_nativeNodeInfo(JNIEnv* env, jclass cls,
                                         jint index)
{
    XAndroidA11yNode node;
    jclass stringClass;
    jobjectArray result;
    char flagsText[16];
    char numText[16];
    jstring items[11];

    (void)cls;
    if (!XAndroid_a11yNodeInfo((int)index, &node)) return NULL;

    stringClass = (*env)->FindClass(env, "java/lang/String");
    if (!stringClass) return NULL;
    result = (*env)->NewObjectArray(env, 11, stringClass, NULL);
    if (!result) return NULL;

    snprintf(flagsText, sizeof(flagsText), "%d", node.flags);
    snprintf(numText, sizeof(numText), "%d", node.parent);
    items[0] = (*env)->NewStringUTF(env, node.className);
    items[1] = (*env)->NewStringUTF(env, node.text);
    items[2] = (*env)->NewStringUTF(env, node.description);
    items[3] = (*env)->NewStringUTF(env, flagsText);
    snprintf(numText, sizeof(numText), "%d", node.x);
    items[4] = (*env)->NewStringUTF(env, numText);
    snprintf(numText, sizeof(numText), "%d", node.y);
    items[5] = (*env)->NewStringUTF(env, numText);
    snprintf(numText, sizeof(numText), "%d", node.w);
    items[6] = (*env)->NewStringUTF(env, numText);
    snprintf(numText, sizeof(numText), "%d", node.h);
    items[7] = (*env)->NewStringUTF(env, numText);
    snprintf(numText, sizeof(numText), "%d", node.parent);
    items[8] = (*env)->NewStringUTF(env, numText);
    snprintf(numText, sizeof(numText), "%d", node.firstChild);
    items[9] = (*env)->NewStringUTF(env, numText);
    snprintf(numText, sizeof(numText), "%d", node.childCount);
    items[10] = (*env)->NewStringUTF(env, numText);

    for (int i = 0; i < 11; ++i) {
        if (!items[i]) items[i] = (*env)->NewStringUTF(env, "");
        (*env)->SetObjectArrayElement(env, result, i, items[i]);
    }
    return result;
}

static jboolean xa11y_nativePerformClick(JNIEnv* env, jclass cls, jint index)
{
    (void)env; (void)cls;
    return XAndroid_a11yPostAction((int)index, XA11Y_ACTION_CLICK)
               ? JNI_TRUE : JNI_FALSE;
}

static jboolean xa11y_nativePerformFocus(JNIEnv* env, jclass cls, jint index)
{
    (void)env; (void)cls;
    return XAndroid_a11yPostAction((int)index, XA11Y_ACTION_FOCUS)
               ? JNI_TRUE : JNI_FALSE;
}

static jint xa11y_nativeNodeCount(JNIEnv* env, jclass cls)
{
    (void)env; (void)cls;
    return (jint)XAndroid_a11yNodeCount();
}

static jint xa11y_nativeRootIndex(JNIEnv* env, jclass cls)
{
    (void)env; (void)cls;
    return (jint)XAndroid_a11yRootIndex();
}

/** @brief JNI 方法绑定表（RegisterNatives 用）。 */
static const JNINativeMethod g_xa11yMethods[] = {
    { "nativeNodeInfo",
      "(I)[Ljava/lang/String;",
      (void*)xa11y_nativeNodeInfo },
    { "nativePerformClick", "(I)Z", (void*)xa11y_nativePerformClick },
    { "nativePerformFocus", "(I)Z", (void*)xa11y_nativePerformFocus },
    { "nativeNodeCount", "()I", (void*)xa11y_nativeNodeCount },
    { "nativeRootIndex", "()I", (void*)xa11y_nativeRootIndex }
};

/** @brief 库加载入口：把 C 函数显式绑到 Java 方法名。
 *  @details 链接含 -Wl,--gc-sections，未被引用的 `Java_...` 命名符号会被
 *           裁掉，ART 按名查找会报 "No implementation found"（真机实证）。
 *           RegisterNatives 不依赖动态符号表，故必须走这条注册路径。
 *           JNI_OnLoad 本身也需保活：used 属性 + 链接期
 *           -Wl,--undefined=JNI_OnLoad（见 CMakeLists.txt 安卓分支）。 */
JNIEXPORT jint JNICALL __attribute__((used))
JNI_OnLoad(JavaVM* vm, void* reserved)
{
    JNIEnv* env = NULL;
    jclass bridgeClass;
    (void)reserved;
    if (!vm) return JNI_ERR;
    if ((*vm)->GetEnv(vm, (void**)&env, JNI_VERSION_1_6) != JNI_OK) {
        if ((*vm)->AttachCurrentThread(vm, &env, NULL) != JNI_OK)
            return JNI_ERR;
    }
    if (!env) return JNI_ERR;
    bridgeClass = (*env)->FindClass(env, XA11Y_BRIDGE_CLASS);
    if (!bridgeClass) {
        (*env)->ExceptionClear(env);
        return JNI_ERR;
    }
    if ((*env)->RegisterNatives(env, bridgeClass, g_xa11yMethods,
                               (jint)(sizeof(g_xa11yMethods) /
                                      sizeof(g_xa11yMethods[0]))) != JNI_OK) {
        (*env)->ExceptionClear(env);
        return JNI_ERR;
    }
    return JNI_VERSION_1_6;
}

#endif /* XWINDOW_ON && XACCESSIBLE_ON && __ANDROID__ */
