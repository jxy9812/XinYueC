/* ==================== STM32 newlib 存根（家族通用） ====================
 * newlib-nano 的系统调用存根，STM32 家族固件通用：
 * - _write：逐字符转发 board_putc（由板级提供，通常接 USART/ITM）；
 * - _sbrk：使用链接脚本提供的 __heap_start/__heap_limit（如 CCM 内
 *   newlib 辅助堆段）；不提供时自行定义该对符号亦可；
 * - 其余：字符设备最小语义（_fstat=CHR/_isatty=1），文件系统类一律失败。
 *
 * 板级契约：板级需提供 void board_putc(char c)（board_sys.h 已声明）。
 */
#include <stdint.h>
#include <stddef.h>
#include <sys/stat.h>

extern int board_putc(char c);

__attribute__((used)) int _write(int fd, const char* buf, int len)
{
    int i;
    (void)fd;
    for (i = 0; i < len; ++i)
        board_putc(buf[i]);
    return len;
}

__attribute__((used)) int _read(int fd, char* buf, int len)
{
    (void)fd; (void)buf; (void)len;
    return 0;
}

__attribute__((used)) int _close(int fd) { (void)fd; return -1; }

__attribute__((used)) int _fstat(int fd, struct stat* st)
{
    (void)fd;
    st->st_mode = S_IFCHR;
    return 0;
}

__attribute__((used)) int _isatty(int fd) { (void)fd; return 1; }

__attribute__((used)) int _lseek(int fd, int off, int whence)
{
    (void)fd; (void)off; (void)whence;
    return 0;
}

__attribute__((used)) void _exit(int code)
{
    (void)code;
    for (;;)
        ;
}

__attribute__((used)) int _kill(int pid, int sig)
{
    (void)pid; (void)sig;
    return -1;
}

__attribute__((used)) int _getpid(void) { return 1; }

/* libc 堆（newlib 辅助堆，链接段 .sysheap/等由板级链接脚本划出）：
 * 请求即从 __heap_start 顺序分配；耗尽返回 -1 拒绝。 */
extern unsigned char __heap_start;
extern unsigned char __heap_limit;

__attribute__((used)) void* _sbrk(int inc)
{
    static unsigned char* brk = &__heap_start;
    unsigned char* old = brk;
    if (inc > 0 && (size_t)inc > (size_t)(&__heap_limit - brk))
        return (void*)-1;
    brk += inc;
    return old;
}
