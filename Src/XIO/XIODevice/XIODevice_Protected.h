#ifndef XIODevice_Protected_H
#define XIODevice_Protected_H
#ifdef __cplusplus
extern "C" {
#endif
/**
 * @brief 虚函数：从设备读取原始数据（纯虚函数，子类必须实现）
 * @param self 指向 XIODevice 对象的指针
 * @param data 指向用于存放读取数据的缓冲区
 * @param maxlen 要读取的最大字节数
 * @return 实际读取的字节数
 */
int64_t XIODevice_readData_base(XIODevice* self, char* data, int64_t maxlen);

/**
 * @brief 虚函数：向设备写入原始数据（纯虚函数，子类必须实现）
 * @param self 指向 XIODevice 对象的指针
 * @param data 指向要写入的数据缓冲区
 * @param len 要写入的字节数
 * @return 实际写入的字节数
 */
int64_t XIODevice_writeData_base(XIODevice* self, const char* data, int64_t len);

/**
 * @brief 设置设备的错误描述字符串
 * @param self 指向 XIODevice 对象的指针
 * @param str 错误描述的 C 字符串，若为 NULL 则清空错误信息
 */
void XIODevice_setErrorString(XIODevice* self, const char* str);
/**
 * @brief 虚函数：从输入流中跳过原始数据
 * @param self 指向 XIODevice 对象的指针
 * @param maxSize 要跳过的最大字节数
 * @return 实际跳过的字节数
 */
int64_t XIODevice_skipData_base(XIODevice* self, int64_t maxSize);

/**
 * @brief 虚函数：从设备读取一行原始数据
 * @param self 指向 XIODevice 对象的指针
 * @param data 指向用于存放读取数据的缓冲区
 * @param maxlen 要读取的最大字节数
 * @return 实际读取的字节数
 */
int64_t XIODevice_readLineData_base(XIODevice* self, char* data, int64_t maxlen);

/**
 * @brief 设置设备的 XFd 文件描述符（仅供子类使用）
 * @param self 指向 XIODevice 对象的指针
 * @param fd 要设置的文件描述符
 */
void XIODevice_setFd(XIODevice* self, XFd fd);

#ifdef __cplusplus
}
#endif
#endif