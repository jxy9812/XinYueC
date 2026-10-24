/**
 * @file       XAndroidJavaPackage.h
 * @brief      框架安卓平台层的 Java 包名定义（JNI 查类的唯一字符串来源）。
 * @details    [2026-10-24 宏化] 此前 JNI 查类在 C 侧**逐处硬编码**包名
 *             （XSystemAndroid.c 8 处 + 无障碍桥 1 处），改一次包名要改 9 个
 *             字符串，漏一处就是运行期 ClassNotFoundException。现收敛到本
 *             头文件的两个宏：
 *               - XANDROID_JAVA_PKG        点形态（loadClass 用）
 *               - XANDROID_JAVA_PKG_SLASH  斜杠形态（JNI FindClass 用）
 *             两者由 XANDROID_JAVA_CLASS / XANDROID_JAVA_JNI_CLASS 拼出类全名。
 *
 *             **Java 源码里没有预处理器**，`package com.xinyuec.android;`
 *             那行必须是字面量，无法宏化——所以包名的权威仍是 Java 源
 *             （Drive/Android/java/com/*.java 的 package 声明），
 *             本头文件只是 C 侧的**唯一字符串来源**，由构建期门禁
 *             （platforms/android/build_apk.sh 与 pack_apk.ps1 的
 *             "manifest <-> platform Activity drift check"）校验：
 *             **C 宏 == Java package == 清单 activity 名**，三者必须一致。
 *
 *             注：Java 源目录是 `Drive/Android/java/com/`（扁平源根布局，
 *             用户指定），**不再镜像包名**；javac 显式给文件路径编译，
 *             -d 输出仍落在 com/xinyuec/android/ 下，dex 与 JNI 查类不受影响。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XANDROIDJAVAPACKAGE_H
#define XANDROIDJAVAPACKAGE_H

/** 框架安卓平台层的 Java 包（点形态）。 */
#define XANDROID_JAVA_PKG       "com.xinyuec.android"

/** 同上，斜杠形态（与 JNI FindClass 的类名格式一致）。 */
#define XANDROID_JAVA_PKG_SLASH "com/xinyuec/android"

/**
 * @brief  拼出类的全名（点形态）——供 Activity.getClassLoader().loadClass 用。
 * @param  name  简单类名，如 "XNetAdapter"。
 */
#define XANDROID_JAVA_CLASS(name) XANDROID_JAVA_PKG "." name

/**
 * @brief  拼出类的全名（斜杠形态）——供 JNI FindClass 用。
 * @param  name  简单类名，如 "XGuiA11yBridge"。
 */
#define XANDROID_JAVA_JNI_CLASS(name) XANDROID_JAVA_PKG_SLASH "/" name

#endif /* XANDROIDJAVAPACKAGE_H */
