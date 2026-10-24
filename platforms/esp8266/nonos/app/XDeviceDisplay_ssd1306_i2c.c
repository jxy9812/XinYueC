/**
 * @file XDeviceDisplay_ssd1306_i2c.c
 * @brief SSD1306 128x64 单色 OLED 的 I2C 软件时序驱动（ESP8266 板级）。
 * @details 引脚/时序/初始化 = build/arduino_oled3/app.c 真机验证版（定版参数，
 *          勿再改动）：SDA=GPIO14(D6)、SCL=GPIO12(D5)，7 位地址 0x3C（写
 *          一字节 0x78）。
 *          引脚配置走 GPF 显式地址：GPIO14=0x6000080C、GPIO12=0x60000804，
 *          值 0xB0（func=GPIO 位4-5=11 | 上拉 bit7）+ GPC bit2 开漏；驱动
 *          低=GPES 置输出使能，释放=GPEC 关输出使能，电平读 GPI（Arduino
 *          core_esp8266_si2c 同构，旧版 GPIO4/5 + GPIO_OUTPUT_SET 仿真为
 *          屏幕不亮根因，已废弃）。
 *          时序 busywait(dcount=142) ≈ 50kHz@80MHz（公式标定值，oled3 同
 *          款）；SCL 释放后有时钟延展有界自旋。
 *          事务 = START + 0x78 + 控制字节（0x00 命令 / 0x40 数据）+ 数据 +
 *          STOP，与 oled3 验证版同形；命令/数据逐字节单事务（真机验证形态）。
 *          init 序列照抄 oled3 的 26 字节（含 2E 关滚动、20 00 水平寻址、
 *          81 FF 对比度），每字节独立事务 + 事务间 SDA 充电等待（≤200 单
 *          位）+ 喂狗；先做 ACK 诊断（S 78 00 AE P，失败重试 5 轮）。
 *          帧缓冲 1024 字节按 SSD1306 页布局（byte = 8 个纵向像素，LSB 在
 *          上）。RGB565→1bpp 阈值转换见 blit565；flush 把脏页按页寻址写入
 *          GDDRAM。每次初始化/逐页发送都打 [i2c] 轨迹（串口可审计）。
 */
#include <stdint.h>
#include "c_types.h" /* SDK 基础类型 */

/* uart0_puts：gui_main.c 提供（74880 直写 FIFO 带水位流控）。 */
extern void uart0_puts(const char* s);
/* ets_intr_lock / ets_intr_unlock：ROM 符号，由链接 --defsym 钉在
 * 0x40000f74 / 0x40000f80（build_fw_final.sh:222-223）。位操作 I2C 的
 * 采样窗口只有几十个 nop，WiFi/lwip 的毫秒定时中断插进「释放 SDA」与
 * 「采样 SDA」之间就会把 ACK 读成 NACK，故整个事务（twi_start..twi_stop）
 * 必须在关中断里跑完。 */
extern unsigned int ets_intr_lock(void);
extern void ets_intr_unlock(void);
/* gui_wdt_feed：gui_stubs.c 寄存器直写喂狗（本固件不链 libpp，与基线
 * 心跳同款；长循环内必须喂，否则 8s 复位）。 */
extern void gui_wdt_feed(void);

/* ---- 引脚（oled3 真机验证定版） ---- */
#define I2C_SDA_PIN 14
#define I2C_SCL_PIN 12
#define SSD1306_WIDTH 128
#define SSD1306_HEIGHT 64
#define SSD1306_PAGES (SSD1306_HEIGHT / 8)
#define SSD1306_ADDR_W 0x78 /* 7 位地址 0x3C 左移 1 + R/W=0 */

/* ---- 寄存器（抄 esp8266_peri.h / oled3 app.c） ---- */
#define ESP_REG(a) (*(volatile unsigned int*)(0x60000000u + (a)))
#define GPES ESP_REG(0x310) /* 输出使能 SET：驱动低 */
#define GPEC ESP_REG(0x314) /* 输出使能 CLR：释放（高阻，靠上拉） */
#define GPI ESP_REG(0x318)  /* 输入电平 */
#define GPC_RD_(p) (*(volatile unsigned int*)(0x60000328u + ((unsigned int)(p) * 4u)))
#define GPC_WR(p, v) \
    ((*(volatile unsigned int*)(0x60000328u + ((unsigned int)(p) * 4u))) = (unsigned int)(v))
#define GPCI 7  /* GPC 中断源域位基（保留原值） */
#define GPCD 2  /* GPC 开漏驱动位 */
#define GPFPU 7 /* GPF 上拉使能位 */
/* GPF 显式地址（oled3 pin_mode_input_pullup 映射表，仅本驱动用到的引脚） */
static unsigned int gpf_addr(int p)
{
    if (p == 12) return 0x60000804u;
    if (p == 14) return 0x6000080Cu;
    return 0u;
}
/* GPFFS(GPFFS_GPIO(p)) @ p=12/14：func=3 → GPFFS 位4-5=11 → 0x30；| 上拉
 * bit7 → 0xB0（oled3 验证值）。 */
#define GPF_GPIO_PULLUP 0xB0u

/* ---- 引脚被抢的探针寄存器（第 2 轮新增，见文件头说明） ----
 * 实测矛盾（cap_R10_shell.log:57）：
 *   GPF12=GPF14=0x000000B0（func=GPIO|上拉，正确）、GPC12=GPC14=0x04
 *   （开漏位，正确），可 GPI 读回 idle sda=0 scl=1 —— 内部上拉配好了仍
 *   读 0，说明有别的输出把脚钉在低电平。ESP8266 上唯一能【硬件绕过 GPF】
 *   直接驱动 GPIO12~15 的是 HSPI（SPI 外设）：GPIO14=HSPI CLK（空闲低）、
 *   GPIO12=HSPI MISO（空闲输入=上拉读 1），与 sda=0/scl=1 完全吻合。
 * 选路的开关是 IO_SWAP 寄存器 bit5 = IOSWAPHS（esp8266_peri.h:184
 * 「Sets HSPI with higher prio」，寄存器 = ESP8266_DREG(0x28) = 0x3FF00028）。
 * 因此 reclaim 步骤：bit5 为 1 就清掉，再重新断言 GPF/GPC。 */
#define IO_SWAP        (*(volatile unsigned int*)0x3FF00028u)
#define IO_SWAP_HSPI   5u
#define HSPI_CR0       (*(volatile unsigned int*)0x60000300u) /* SPI/HSPI 控制 */
#define HSPI_CR1       (*(volatile unsigned int*)0x60000304u)
#define SPI1_CR0       (*(volatile unsigned int*)0x60000320u) /* 第二个 SPI 主 */

/* ---- 状态 ---- */
static uint8_t s_fb[SSD1306_WIDTH * SSD1306_PAGES];
static uint16_t s_dirtyPages; /* bit p = 页 p 待发送 */
static unsigned s_ackErrors;
static unsigned s_pageWrites;
static int s_initFailedAt = -1; /* init 序列字节下标（0 起），-1=未失败 */

/* 前置声明：earlyInit / testPattern 在 init 之前定义，但要用到 init/flush。 */
int XDeviceDisplay_ssd1306_i2c_init(void);
void XDeviceDisplay_ssd1306_i2c_flush(void);

/* ---------------- 时序（oled3 busywait，dcount=142 ≈ 50kHz@80MHz） ---------------- */

static unsigned int twi_dcount = 142;

static void busywait(unsigned int v)
{
    unsigned int i;
    for (i = 0; i < v; i++)
        __asm__ __volatile__("nop");
}

/* oled3 同款软毫秒（空转循环，量级校准过；仅用于充电等待/重试退避）。 */
static void delay_ms(int ms)
{
    while (ms--) {
        volatile int i;
        for (i = 0; i < 2000; i++) { }
    }
}

/* ---------------- 开漏原语（抄 oled3 / Arduino si2c） ---------------- */

#define SDA_LOW(x) (GPES = (1u << (x)))  /* 输出使能 → 驱动低 */
#define SDA_HIGH(x) (GPEC = (1u << (x))) /* 关输出 → 释放高 */
#define SCL_LOW(x) (GPES = (1u << (x)))
#define SCL_HIGH(x) (GPEC = (1u << (x)))
#define SDA_READ(x) (((GPI & (1u << (x))) != 0) ? 1 : 0)
#define SCL_READ(x) (((GPI & (1u << (x))) != 0) ? 1 : 0)

static void wait_clock_stretch(void)
{
    /* 从机拉低 SCL 时自旋等待，带上限（oled3 同款 ~150ms 量级）。 */
    volatile unsigned long n = 3000000UL;
    while (!SCL_READ(I2C_SCL_PIN) && n--) { }
}

/* ---------------- I2C 字节事务（oled3 原样） ---------------- */

/* 返回 1=START 成功；0=总线忙（SDA 未回高，拒绝发起）。 */
static int twi_start(void)
{
    SCL_HIGH(I2C_SCL_PIN);
    SDA_HIGH(I2C_SDA_PIN);
    if (!SDA_READ(I2C_SDA_PIN))
        return 0;
    busywait(twi_dcount);
    SDA_LOW(I2C_SDA_PIN);
    busywait(twi_dcount);
    SCL_LOW(I2C_SCL_PIN);
    busywait(twi_dcount);
    return 1;
}

static void twi_stop(void)
{
    SCL_LOW(I2C_SCL_PIN);
    SDA_LOW(I2C_SDA_PIN);
    busywait(twi_dcount);
    SCL_HIGH(I2C_SCL_PIN);
    wait_clock_stretch();
    busywait(twi_dcount);
    SDA_HIGH(I2C_SDA_PIN);
    busywait(twi_dcount);
}

static void twi_write_bit(int bit)
{
    SCL_LOW(I2C_SCL_PIN);
    if (bit)
        SDA_HIGH(I2C_SDA_PIN);
    else
        SDA_LOW(I2C_SDA_PIN);
    busywait(twi_dcount + 1);
    SCL_HIGH(I2C_SCL_PIN);
    wait_clock_stretch();
    busywait(twi_dcount);
}

static int twi_read_bit(void)
{
    int bit;
    SCL_LOW(I2C_SCL_PIN);
    SDA_HIGH(I2C_SDA_PIN);
    busywait(twi_dcount + 2);
    SCL_HIGH(I2C_SCL_PIN);
    wait_clock_stretch();
    bit = SDA_READ(I2C_SDA_PIN);
    busywait(twi_dcount);
    return bit;
}

/* 返回 1=ACK，0=NACK（twi_read_bit 返回 SDA 电平：高=1=NACK，低=0=ACK，
 * oled3 语义 return !twi_read_bit()）。 */
static int twi_write_byte(unsigned char b)
{
    int i;
    for (i = 0; i < 8; i++) {
        twi_write_bit((b & 0x80) ? 1 : 0);
        b <<= 1;
    }
    if (twi_read_bit()) {
        ++s_ackErrors;
        return 0;
    }
    return 1;
}

/* ---------------- 引脚配置（抄 oled3：GPF 显式地址 + GPC 开漏） ---------------- */

static void pin_mode_input_pullup(int p)
{
    unsigned int mux = gpf_addr(p);
    if (!mux)
        return;
    /* GPF = func=GPIO | 上拉；关输出使能；GPC 保留中断源域 + 置开漏位。 */
    *(volatile unsigned int*)mux = GPF_GPIO_PULLUP;
    GPEC = (1u << p);
    GPC_WR(p, (GPC_RD_(p) & (0xFu << GPCI)) | (1u << GPCD));
}

/* 每次 flush 前重新【抢回】引脚（ask 第 2 轮 0③）：只是两条 GPF + 一条
 * GPEC + 两条 GPC 读改写，成本可忽略。步骤：
 *   1) IO_SWAP.bit5(IOSWAPHS) 若为 1，说明 HSPI 抢占了 GPIO12~15 —— 硬件
 *      会绕过 GPF 直接把 CLK 驱动到 GPIO14（空闲低），这正是 idle sda=0
 *      的候选根因，清掉它；
 *   2) 无论上一步结果如何，重新断言 GPF=0xB0 / GPEC / GPC 开漏。
 * 返回 1=本次清过 IO_SWAP（供调用方打日志）。 */
static int i2c_reclaim_pins(void)
{
    int cleared = 0;
    if (IO_SWAP & (1u << IO_SWAP_HSPI)) {
        IO_SWAP &= ~(1u << IO_SWAP_HSPI);
        cleared = 1;
    }
    pin_mode_input_pullup(I2C_SDA_PIN);
    pin_mode_input_pullup(I2C_SCL_PIN);
    return cleared;
}

/* ---------------- 轨迹日志（[i2c] 前缀，串口可审计；低量摘要） ---------------- */

static void i2c_log_str(const char* s) { uart0_puts(s); }

static void i2c_log_hex(unsigned int v)
{
    static const char kHex[] = "0123456789ABCDEF";
    char b[11];
    int i;
    b[0] = '0';
    b[1] = 'x';
    for (i = 0; i < 8; i++)
        b[2 + i] = kHex[(v >> ((7 - i) * 4)) & 0xFu];
    b[10] = '\0';
    uart0_puts(b);
}

static void i2c_log_dec(unsigned int v)
{
    char b[12];
    int i = 10;
    b[11] = '\0';
    if (!v)
        b[--i] = '0';
    while (v && i > 0) {
        b[--i] = (char)('0' + (v % 10));
        v /= 10;
    }
    uart0_puts(&b[i]); /* 从首个数字起，紧邻 b[11]='\0'，无栈垃圾 */
}

/* 引脚寄存器快照：定位「同驱动、同参数、换个固件就读回 SDA=0」时是谁把
 * mux/开漏改了。GPF12=0x60000804、GPF14=0x6000080C，期望
 * 0xB0000000（func=GPIO|上拉）；GPC12/14 期望 bit2=开漏。 */
static void i2c_dump_pins(const char* tag)
{
    i2c_log_str("[i2c] ");
    i2c_log_str(tag);
    i2c_log_str(" GPF12=");
    i2c_log_hex(*(volatile unsigned int*)0x60000804u);
    i2c_log_str(" GPF14=");
    i2c_log_hex(*(volatile unsigned int*)0x6000080Cu);
    i2c_log_str(" GPC12=");
    i2c_log_hex(GPC_RD_(I2C_SCL_PIN));
    i2c_log_str(" GPC14=");
    i2c_log_hex(GPC_RD_(I2C_SDA_PIN));
    i2c_log_str(" IOSWAP=");
    i2c_log_hex(IO_SWAP);
    i2c_log_str(" HSPIcr0=");
    i2c_log_hex(HSPI_CR0);
    i2c_log_str(" SPI1cr0=");
    i2c_log_hex(SPI1_CR0);
    i2c_log_str(" GPES=");
    i2c_log_hex(GPES);
    i2c_log_str(" GPI=");
    i2c_log_hex(GPI);
    i2c_log_str(" idle sda=");
    i2c_log_dec((unsigned)SDA_READ(I2C_SDA_PIN));
    i2c_log_str(" scl=");
    i2c_log_dec((unsigned)SCL_READ(I2C_SCL_PIN));
    i2c_log_str("\r\n");
}

/* ---- 总线解锁（标准 9-clock recovery）----
 * 场景（cap_title_retry.log 00:41 实锤）：上一次 I2C 事务进行到一半时芯片
 * 被复位（esptool 烧录复位/看门狗），SSD1306 从机侧停在「收到半个字节、
 * 等剩余时钟」的状态，把 SDA 钉在低电平。此后每次 boot 的 twi_start() 都
 * 因 SDA 不回高而拒绝发起 → init FAIL → 面板一直保持断电前的内容（用户
 * 看到的「黄蓝条纹/方框」反复重启也不消失，只有整板断电能解）。
 * 标准解法：SCL 手动补打最多 9 个时钟，从机位计数器走完后自行释放 SDA，
 * 再补一个 STOP 把总线拉回 idle。返回 1=SDA 已回高（含本就空闲），
 * 0=打满 9 个时钟仍卡低（硬件级故障，只能断电）。 */
static int i2c_bus_unlock(void)
{
    int i;
    SCL_HIGH(I2C_SCL_PIN);
    busywait(twi_dcount);
    if (SDA_READ(I2C_SDA_PIN))
        return 1;
    for (i = 0; i < 9; ++i) {
        SCL_LOW(I2C_SCL_PIN);
        busywait(twi_dcount);
        SCL_HIGH(I2C_SCL_PIN);
        wait_clock_stretch();
        busywait(twi_dcount);
        if (SDA_READ(I2C_SDA_PIN))
            break;
    }
    if (!SDA_READ(I2C_SDA_PIN)) {
        i2c_log_str("[i2c] bus unlock FAIL: SDA still low after 9 clocks\r\n");
        return 0;
    }
    /* STOP：SCL 高电平期间 SDA 低→高，从机接口状态机归位。 */
    SCL_LOW(I2C_SCL_PIN);
    SDA_LOW(I2C_SDA_PIN);
    busywait(twi_dcount);
    SCL_HIGH(I2C_SCL_PIN);
    wait_clock_stretch();
    busywait(twi_dcount);
    SDA_HIGH(I2C_SDA_PIN);
    busywait(twi_dcount);
    i2c_log_str("[i2c] bus unlock: SDA released after ");
    i2c_log_dec((unsigned)i);
    i2c_log_str(" clocks\r\n");
    return 1;
}

/* ---------------- SSD1306 命令/数据通道（oled3 逐字节单事务形态） ---------------- */

/* 命令事务：S + 0x78W + 0x00 + cmd + P。返回 NACK 字节数。
 * 整段关中断：位操作采样的时间窗太窄，见文件头 ets_intr_lock 注释。
 * 锁内不得调 uart0_puts / gui_wdt_feed（前者走 UART0 FIFO 阻塞，后者是
 * 软件狗寄存器直写但会拖长关中断窗口）。 */
static int ssd1306_cmd_byte(unsigned char cmd)
{
    int nack = 0;
    unsigned int key = ets_intr_lock();
    if (!twi_start()) {
        ets_intr_unlock();
        ++s_ackErrors;
        return 3;
    }
    if (!twi_write_byte(SSD1306_ADDR_W))
        ++nack;
    if (!twi_write_byte(0x00))
        ++nack;
    if (!twi_write_byte(cmd))
        ++nack;
    twi_stop();
    ets_intr_unlock();
    return nack;
}

/* 数据事务：S + 0x78W + 0x40 + byte + P（oled3 数据路径真机验证形态）。
 * 返回 NACK 字节数。 */
static int ssd1306_data_byte(unsigned char v)
{
    int nack = 0;
    unsigned int key = ets_intr_lock();
    if (!twi_start()) {
        ets_intr_unlock();
        ++s_ackErrors;
        return 3;
    }
    if (!twi_write_byte(SSD1306_ADDR_W))
        ++nack;
    if (!twi_write_byte(0x40))
        ++nack;
    if (!twi_write_byte(v))
        ++nack;
    twi_stop();
    ets_intr_unlock();
    return nack;
}

/* 诊断探针：只发一条 S + 0x78 + 0x00 + 0xAE + P，返回 1=三个字节全 ACK。
 * 不改帧缓冲、不动 s_dirtyPages，供「开机最前端」做对照实验用。 */
int XDeviceDisplay_ssd1306_i2c_probe(void)
{
    int r1, r2, r3, ok;
    unsigned int key = ets_intr_lock();
    if (!twi_start()) {
        ets_intr_unlock();
        i2c_log_str("[i2c] probe start refused (SDA stuck low)\r\n");
        return 0;
    }
    r1 = twi_write_byte(SSD1306_ADDR_W);
    r2 = twi_write_byte(0x00);
    /* 探针命令必须是【无副作用】的：0xE0 = NOP。
     * ⚠曾用 kInit[0]=0xAE 探测，那正是"关显示"命令——gui_oled_init() 以探针
     * 结果决定是否跳过 re-init，探针一发 0xAE 就把关屏放在最后，没人再发 0xAF，
     * 表现为 I2C 全部 ACK、页写 nack=0、flush done，屏幕却一直黑。 */
    r3 = twi_write_byte(0xE0);
    twi_stop();
    ets_intr_unlock();
    ok = (r1 && r2 && r3) ? 1 : 0;
    i2c_log_str("[i2c] probe addr=");
    i2c_log_dec((unsigned)r1);
    i2c_log_str(" ctrl=");
    i2c_log_dec((unsigned)r2);
    i2c_log_str(" cmd=");
    i2c_log_dec((unsigned)r3);
    i2c_log_str(ok ? " ACK\r\n" : " NACK\r\n");
    return ok;
}

/* 开机最前端用：配引脚 + 稳定等待 + 探针，并把 init 序列发完（不因探针失败
 * 而中止，见 XDeviceDisplay_ssd1306_i2c_init 的说明）。返回 0=init 全 ACK。 */
int XDeviceDisplay_ssd1306_i2c_earlyInit(void)
{
    (void)i2c_reclaim_pins();
    i2c_dump_pins("early-after-pinmode");
    (void)i2c_bus_unlock();   /* 复位打断半截事务后的楔死自愈（见函数头说明） */
    (void)XDeviceDisplay_ssd1306_i2c_probe();
    return XDeviceDisplay_ssd1306_i2c_init();
}

/* 测试图样：帧缓冲打「标题栏基线 + 外边框」+ 全页 flush。开机最前端把屏
 * 点亮用（证明 I2C 通路在 XGui/lwip/WiFi 起来之前就是好的），之后被 XGui
 * 的正常绘制覆盖。
 * ⚠不再用 4x16 棋盘：棋盘在黄蓝屏上的观感就是「条纹+方框」，与总线楔死时
 * 面板里的断电残留肉眼无法区分，用户曾把诊断图样误判为「故障依旧」。白条
 * +边框是确定性图形，断电垃圾长不出这种形状。 */
void XDeviceDisplay_ssd1306_i2c_testPattern(void)
{
    int p, x;
    for (p = 0; p < SSD1306_PAGES; ++p)
        for (x = 0; x < SSD1306_WIDTH; ++x) {
            uint8_t v;
            if (p < 2)
                v = 0xFF;                       /* 标题栏：行 0-15 全亮 */
            else if (x == 0 || x == SSD1306_WIDTH - 1)
                v = 0xFF;                       /* 左右边框 */
            else if (p == SSD1306_PAGES - 1)
                v = 0x80;                       /* 底边框：行 63 */
            else
                v = 0x00;
            s_fb[p * SSD1306_WIDTH + x] = v;
        }
    s_dirtyPages = 0xFFu;
    XDeviceDisplay_ssd1306_i2c_flush();
}

/* ---------------- 对外 API ---------------- */

/* 上电初始化：引脚配置 → ACK 诊断（重试 5 轮）→ oled3 26 字节 init 序列
 * （逐字节事务 + 充电等待）。返回 0=全 ACK。 */
int XDeviceDisplay_ssd1306_i2c_init(void)
{
    /* 照抄 oled3 真机验证 init 序列（26 字节，含 2E 关滚动）。 */
    static const unsigned char kInit[] = {
        0xAE, 0xD5, 0x80, 0xA8, 0x3F, 0xD3, 0x00, 0x40, 0x8D, 0x14,
        0x20, 0x00, 0xA1, 0xC8, 0xDA, 0x12, 0x81, 0xFF, 0xD9, 0xF1,
        0xDB, 0x40, 0xA4, 0xA6, 0x2E, 0xAF
    };
    int i;
    int n = (int)sizeof(kInit);
    int nack = 0;

    for (i = 0; i < (int)sizeof(s_fb); ++i)
        s_fb[i] = 0;
    s_dirtyPages = 0;
    s_initFailedAt = -1;

    /* 引脚配置 + 稳定等待（oled3：2M 空转 + 喂狗）。 */
    i2c_dump_pins("pre-pinmode");
    if (i2c_reclaim_pins())
        i2c_log_str("[i2c] reclaim: IOSWAP.IOSWAPHS cleared (HSPI was holding GPIO12/14)\r\n");
    {
        volatile unsigned long k;
        for (k = 0; k < 2000000UL; k++) { }
    }
    gui_wdt_feed();

    i2c_log_str("[i2c] pins sda=14 scl=12 idle sda=");
    i2c_log_dec((unsigned)SDA_READ(I2C_SDA_PIN));
    i2c_log_str(" scl=");
    i2c_log_dec((unsigned)SCL_READ(I2C_SCL_PIN));
    i2c_log_str("\r\n");
    i2c_dump_pins("post-pinmode");

    /* ACK 诊断：S + 0x78 + 0x00 + 0xAE + P，失败重试 5 轮。
     * ⚠ 探针失败【不再整体中止 init】：旧行为是一错就 return -1，
     * display-on(0xAF) 永远发不出去 → 屏全黑。改成只记录、继续把 26 字节
     * init 序列发完，让偶发 NACK 也能把屏点亮。 */
    {
        unsigned int key = ets_intr_lock();
        int started = twi_start();
        int r1 = 0, r2 = 0, r3 = 0;
        if (started) {
            r1 = twi_write_byte(SSD1306_ADDR_W);
            r2 = twi_write_byte(0x00);
            r3 = twi_write_byte(0xE0);   /* NOP，不改面板状态（同 probe()） */
            twi_stop();
        }
        ets_intr_unlock();
        if (!started) {
            i2c_log_str("[i2c] probe start refused (SDA stuck low)\r\n");
            /* 楔死自愈：先补时钟解锁再进重试轮；旧代码只重试不解锁，
             * 5 轮全是徒劳（cap_title_retry.log：重试后仍 init NACK=27）。 */
            i2c_bus_unlock();
            if (s_initFailedAt < 0)
                s_initFailedAt = 0;
            ++nack;
        } else if (!(r1 && r2 && r3)) {
            int try_n;
            for (try_n = 0; try_n < 5; try_n++) {
                unsigned int k2;
                gui_wdt_feed();
                delay_ms(100);
                k2 = ets_intr_lock();
                r1 = r2 = r3 = 0;
                if (twi_start()) {
                    r1 = twi_write_byte(SSD1306_ADDR_W);
                    r2 = twi_write_byte(0x00);
                    r3 = twi_write_byte(0xE0);   /* NOP，不改面板状态 */
                    twi_stop();
                }
                ets_intr_unlock();
                if (!r1 && !r2 && !r3)
                    continue;
                i2c_log_str("[i2c] probe retry ");
                i2c_log_dec((unsigned)try_n);
                i2c_log_str(": ");
                i2c_log_dec((unsigned)r1);
                i2c_log_dec((unsigned)r2);
                i2c_log_dec((unsigned)r3);
                i2c_log_str("\r\n");
                if (r1 && r2 && r3)
                    break;
            }
        }
        i2c_log_str("[i2c] probe addr=");
        i2c_log_dec((unsigned)r1);
        i2c_log_str(" ctrl=");
        i2c_log_dec((unsigned)r2);
        i2c_log_str(" cmd=");
        i2c_log_dec((unsigned)r3);
        i2c_log_str("\r\n");
    }

    /* init 序列：每字节独立事务；事务间等 SDA 真正充电回高（内部弱上拉，
     * oled3 关键修正）；逐字节喂狗。 */
    for (i = 0; i < n; i++) {
        int w = 0;
        gui_wdt_feed();
        while (!SDA_READ(I2C_SDA_PIN) && w < 200) {
            delay_ms(1);
            w++;
        }
        if (i == 0 || i == 1) {
            i2c_log_str("[i2c] charge-wait cmd");
            i2c_log_dec((unsigned)i);
            i2c_log_str("=");
            i2c_log_dec((unsigned)w);
            i2c_log_str("\r\n");
        }
        if (ssd1306_cmd_byte(kInit[i]) != 0) {
            if (s_initFailedAt < 0)
                s_initFailedAt = i;
            ++nack;
        }
        delay_ms(2);
    }
    i2c_log_str("[i2c] init NACK=");
    i2c_log_dec((unsigned)nack);
    i2c_log_str("\r\n");
    return nack ? -1 : 0;
}

/* 整屏清黑（帧缓冲清零 + 全页发送）。 */
void XDeviceDisplay_ssd1306_i2c_clear(void)
{
    int p;
    for (p = 0; p < SSD1306_PAGES; ++p)
        s_dirtyPages |= (uint16_t)(1u << p);
    XDeviceDisplay_ssd1306_i2c_flush();
}

/* RGB565 像素块写入帧缓冲（阈值 1bpp：luma>=128 点亮）。坐标为窗口坐标。 */
void XDeviceDisplay_ssd1306_i2c_blit565(int x, int y, int w, int h,
                                        const uint8_t* rgb565, int strideBytes)
{
    int i;
    int j;
    if (!rgb565 || w <= 0 || h <= 0)
        return;
    if (x < 0) { w += x; rgb565 -= x * 2; x = 0; }
    if (y < 0) { h += y; rgb565 -= y * strideBytes; y = 0; }
    if (x + w > SSD1306_WIDTH) w = SSD1306_WIDTH - x;
    if (y + h > SSD1306_HEIGHT) h = SSD1306_HEIGHT - y;
    if (w <= 0 || h <= 0)
        return;
    for (j = 0; j < h; ++j) {
        int py = y + j;
        int page = py >> 3;
        int bit = py & 7;
        const uint16_t* row = (const uint16_t*)(rgb565 + (size_t)j * (size_t)strideBytes);
        uint8_t* fbRow = &s_fb[page * SSD1306_WIDTH];
        for (i = 0; i < w; ++i) {
            uint16_t px = row[x + i];
            unsigned r5 = (px >> 11) & 0x1F;
            unsigned g6 = (px >> 5) & 0x3F;
            unsigned b5 = px & 0x1F;
            unsigned r8 = (r5 << 3) | (r5 >> 2);
            unsigned g8 = (g6 << 2) | (g6 >> 4);
            unsigned b8 = (b5 << 3) | (b5 >> 2);
            unsigned lum = (r8 * 30u + g8 * 59u + b8 * 11u) / 100u;
            if (lum >= 128u)
                fbRow[x + i] |= (uint8_t)(1u << bit);
            else
                fbRow[x + i] &= (uint8_t)~(1u << bit);
        }
        s_dirtyPages |= (uint16_t)(1u << page);
    }
}

/* 增量刷屏：每次只发一页，返回 1=还有脏页待发。
 * 整屏 8 页一次刷完在这块板子上要占约 1.3 秒的忙等（实测：心跳从 5s 周期
 * 被撑到 6.3s），期间 lwip 定时器跑不了，ICMP 丢包能到 30%+。把它摊到多个
 * 事件循环 tick 上，单次阻塞降到约 1/8，事件循环才有喘息机会。 */
int XDeviceDisplay_ssd1306_i2c_flush_step(void)
{
    int p;
    int col;
    for (p = 0; p < SSD1306_PAGES; ++p) {
        if (!(s_dirtyPages & (1u << p)))
            continue;
        s_dirtyPages &= (uint16_t)~(1u << p);
        gui_wdt_feed();
        ssd1306_cmd_byte((unsigned char)(0xB0 | p));
        ssd1306_cmd_byte(0x00);
        ssd1306_cmd_byte(0x10);
        for (col = 0; col < SSD1306_WIDTH; ++col)
            ssd1306_data_byte(s_fb[p * SSD1306_WIDTH + col]);
        ++s_pageWrites;
        i2c_log_str("[i2c] step pg");
        i2c_log_dec((unsigned)p);
        i2c_log_str(" done\r\n");
        return 1;
    }
    return 0;
}

/* 把脏页发送到面板（GDDRAM 写入：页地址/列地址命令事务 + oled3 逐字节
 * 数据事务）。每页一行摘要轨迹。 */
void XDeviceDisplay_ssd1306_i2c_flush(void)
{
    int p;
    /* 每帧重夺引脚：任何 init 之后被别的模块改走 GPIO12/14（HSPI 抢占的
     * 头号嫌疑）都在这里被拉回来，成本 = 5 次寄存器写。 */
    if (i2c_reclaim_pins()) {
        i2c_log_str("[i2c] reclaim: IOSWAP.IOSWAPHS cleared\r\n");
        i2c_dump_pins("post-reclaim");
    }
    for (p = 0; p < SSD1306_PAGES; ++p) {
        int nack;
        int col;
        if (!(s_dirtyPages & (1u << p)))
            continue;
        gui_wdt_feed();
        /* 页寻址三连（oled3 形态：三条独立命令事务，水平寻址模式下逐页
         * 重设仍生效，保证列指针确定性归零）。 */
        nack = ssd1306_cmd_byte((unsigned char)(0xB0 | p));
        nack += ssd1306_cmd_byte(0x00);
        nack += ssd1306_cmd_byte(0x10);
        for (col = 0; col < SSD1306_WIDTH; ++col)
            nack += ssd1306_data_byte(s_fb[p * SSD1306_WIDTH + col]);
        ++s_pageWrites;
        i2c_log_str("[i2c] pg");
        i2c_log_dec((unsigned)p);
        i2c_log_str(" 128B nack=");
        i2c_log_dec((unsigned)nack);
        i2c_log_str(nack ? " NACK\r\n" : " ok\r\n");
    }
    s_dirtyPages = 0;
}

unsigned XDeviceDisplay_ssd1306_i2c_ackErrors(void) { return s_ackErrors; }
unsigned XDeviceDisplay_ssd1306_i2c_pageWrites(void) { return s_pageWrites; }
int XDeviceDisplay_ssd1306_i2c_initFailedAt(void) { return s_initFailedAt; }

/* 帧缓冲只读访问（诊断用）：返回 s_fb（128x64 1bpp，8 页 x 128 字节）。
 * 用于把帧缓冲内容以 ASCII 转储到串口，确认绘制是否真的落进缓冲。 */
const unsigned char* XDeviceDisplay_ssd1306_i2c_fb(void)
{
    return s_fb;
}
