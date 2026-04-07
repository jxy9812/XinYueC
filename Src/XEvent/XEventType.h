#ifndef XEVENTTYPE_H
#define XEVENTTYPE_H
#ifdef __cplusplus
extern "C" {
#endif
typedef enum
{
    XEVENT_TYPE_NONE = 0,                                      // 无事件
    XEVENT_TYPE_TIMER = 1,                                     // 定时器事件
    XEVENT_TYPE_MOUSE_BUTTON_PRESS = 2,                        // 鼠标按键按下
    XEVENT_TYPE_MOUSE_BUTTON_RELEASE = 3,                      // 鼠标按键释放
    XEVENT_TYPE_MOUSE_BUTTON_DBL_CLICK = 4,                    // 鼠标双击
    XEVENT_TYPE_MOUSE_MOVE = 5,                                // 鼠标移动
    XEVENT_TYPE_KEY_PRESS = 6,                                 // 键盘按下
    XEVENT_TYPE_KEY_RELEASE = 7,                               // 键盘释放
    XEVENT_TYPE_FOCUS_IN = 8,                                  // 获得焦点
    XEVENT_TYPE_FOCUS_OUT = 9,                                 // 失去焦点
    XEVENT_TYPE_FOCUS_ABOUT_TO_CHANGE = 23,                    // 焦点即将改变
    XEVENT_TYPE_ENTER = 10,                                    // 鼠标进入控件
    XEVENT_TYPE_LEAVE = 11,                                    // 鼠标离开控件
    XEVENT_TYPE_PAINT = 12,                                    // 重绘事件
    XEVENT_TYPE_MOVE = 13,                                     // 控件移动
    XEVENT_TYPE_RESIZE = 14,                                   // 控件调整大小
    XEVENT_TYPE_CREATE = 15,                                   // 控件创建
    XEVENT_TYPE_DESTROY = 16,                                  // 控件销毁
    XEVENT_TYPE_SHOW = 17,                                     // 显示控件
    XEVENT_TYPE_HIDE = 18,                                     // 隐藏控件
    XEVENT_TYPE_CLOSE = 19,                                    // 关闭窗口
    XEVENT_TYPE_QUIT = 20,                                     // 应用退出
    XEVENT_TYPE_PARENT_CHANGE = 21,                            // 父对象变更
    XEVENT_TYPE_PARENT_ABOUT_TO_CHANGE = 131,                  // 父对象即将变更
    XEVENT_TYPE_THREAD_CHANGE = 22,                            // 线程上下文变更
    XEVENT_TYPE_WINDOW_ACTIVATE = 24,                          // 窗口激活
    XEVENT_TYPE_WINDOW_DEACTIVATE = 25,                        // 窗口停用
    XEVENT_TYPE_SHOW_TO_PARENT = 26,                           // 请求父控件显示
    XEVENT_TYPE_HIDE_TO_PARENT = 27,                           // 请求父控件隐藏
    XEVENT_TYPE_WHEEL = 31,                                    // 鼠标滚轮
    XEVENT_TYPE_WINDOW_TITLE_CHANGE = 33,                      // 窗口标题变更
    XEVENT_TYPE_WINDOW_ICON_CHANGE = 34,                       // 窗口图标变更
    XEVENT_TYPE_APPLICATION_WINDOW_ICON_CHANGE = 35,           // 应用窗口图标变更
    XEVENT_TYPE_APPLICATION_FONT_CHANGE = 36,                  // 应用字体变更
    XEVENT_TYPE_APPLICATION_LAYOUT_DIRECTION_CHANGE = 37,      // 应用布局方向变更
    XEVENT_TYPE_APPLICATION_PALETTE_CHANGE = 38,               // 应用调色板变更
    XEVENT_TYPE_PALETTE_CHANGE = 39,                           // 控件调色板变更
    XEVENT_TYPE_CLIPBOARD = 40,                                // 剪贴板操作
    XEVENT_TYPE_SPEECH = 42,                                   // 语音事件
    XEVENT_TYPE_META_CALL = 43,                                // 元对象调用（如 invokeMethod）
    XEVENT_TYPE_SOCK_ACT = 50,                                 // 套接字活动
    XEVENT_TYPE_SHORTCUT_OVERRIDE = 51,                        // 快捷键覆盖
    XEVENT_TYPE_DEFERRED_DELETE = 52,                          // 延迟删除
    XEVENT_TYPE_WIN_EVENT_ACT = 132,                           // Windows 系统事件激活

    // Drag and drop
    XEVENT_TYPE_DRAG_ENTER = 60,                               // 拖拽进入
    XEVENT_TYPE_DRAG_MOVE = 61,                                // 拖拽移动
    XEVENT_TYPE_DRAG_LEAVE = 62,                               // 拖拽离开
    XEVENT_TYPE_DROP = 63,                                     // 拖拽释放（放置）
    XEVENT_TYPE_DRAG_RESPONSE = 64,                            // 拖拽响应

    // Child events
    XEVENT_TYPE_CHILD_ADDED = 68,                              // 子对象添加
    XEVENT_TYPE_CHILD_POLISHED = 69,                           // 子对象 polish 完成
    XEVENT_TYPE_CHILD_REMOVED = 71,                            // 子对象移除

    // Widget polish and layout
    XEVENT_TYPE_SHOW_WINDOW_REQUEST = 73,                      // 显示窗口请求
    XEVENT_TYPE_POLISH_REQUEST = 74,                           // 请求 polish
    XEVENT_TYPE_POLISH = 75,                                   // 执行 polish
    XEVENT_TYPE_LAYOUT_REQUEST = 76,                           // 布局请求
    XEVENT_TYPE_UPDATE_REQUEST = 77,                           // 更新请求
    XEVENT_TYPE_UPDATE_LATER = 78,                             // 延迟更新

    // ActiveX
    XEVENT_TYPE_EMBEDDING_CONTROL = 79,                        // 嵌入 ActiveX 控件
    XEVENT_TYPE_ACTIVATE_CONTROL = 80,                         // 激活 ActiveX 控件
    XEVENT_TYPE_DEACTIVATE_CONTROL = 81,                       // 停用 ActiveX 控件

    // Misc UI
    XEVENT_TYPE_CONTEXT_MENU = 82,                             // 上下文菜单
    XEVENT_TYPE_INPUT_METHOD = 83,                             // 输入法事件
    XEVENT_TYPE_TABLET_MOVE = 87,                              // 数位板移动
    XEVENT_TYPE_LOCALE_CHANGE = 88,                            // 区域设置变更
    XEVENT_TYPE_LANGUAGE_CHANGE = 89,                          // 语言变更
    XEVENT_TYPE_LAYOUT_DIRECTION_CHANGE = 90,                  // 布局方向变更
    XEVENT_TYPE_STYLE = 91,                                    // 样式事件
    XEVENT_TYPE_TABLET_PRESS = 92,                             // 数位板按下
    XEVENT_TYPE_TABLET_RELEASE = 93,                           // 数位板释放
    XEVENT_TYPE_OK_REQUEST = 94,                               // OK 请求（如对话框确认）
    XEVENT_TYPE_HELP_REQUEST = 95,                             // 帮助请求
    XEVENT_TYPE_ICON_DRAG = 96,                                // 图标拖拽
    XEVENT_TYPE_FONT_CHANGE = 97,                              // 字体变更
    XEVENT_TYPE_ENABLED_CHANGE = 98,                           // 启用状态变更
    XEVENT_TYPE_ACTIVATION_CHANGE = 99,                        // 激活状态变更
    XEVENT_TYPE_STYLE_CHANGE = 100,                            // 样式变更
    XEVENT_TYPE_ICON_TEXT_CHANGE = 101,                        // 图标文本变更
    XEVENT_TYPE_MODIFIED_CHANGE = 102,                         // 修改状态变更
    XEVENT_TYPE_MOUSE_TRACKING_CHANGE = 109,                   // 鼠标跟踪状态变更
    XEVENT_TYPE_WINDOW_BLOCKED = 103,                          // 窗口被阻塞
    XEVENT_TYPE_WINDOW_UNBLOCKED = 104,                        // 窗口解除阻塞
    XEVENT_TYPE_WINDOW_STATE_CHANGE = 105,                     // 窗口状态变更（最小化/最大化等）
    XEVENT_TYPE_READ_ONLY_CHANGE = 106,                        // 只读状态变更

    // Tooltips and help
    XEVENT_TYPE_TOOL_TIP = 110,                                // 工具提示
    XEVENT_TYPE_WHATS_THIS = 111,                              // “这是什么”帮助
    XEVENT_TYPE_STATUS_TIP = 112,                              // 状态栏提示

    // Actions
    XEVENT_TYPE_ACTION_CHANGED = 113,                          // 动作变更
    XEVENT_TYPE_ACTION_ADDED = 114,                            // 动作添加
    XEVENT_TYPE_ACTION_REMOVED = 115,                          // 动作移除

    // File and shortcuts
    XEVENT_TYPE_FILE_OPEN = 116,                               // 文件打开
    XEVENT_TYPE_SHORTCUT = 117,                                // 快捷键触发
    XEVENT_TYPE_WHATS_THIS_CLICKED = 118,                      // “这是什么”被点击

    // Toolbar
    XEVENT_TYPE_TOOL_BAR_CHANGE = 120,                         // 工具栏变更

    // Application activation (deprecated)
    XEVENT_TYPE_APPLICATION_ACTIVATE = 121,                    // 应用激活（已弃用）
    XEVENT_TYPE_APPLICATION_ACTIVATED = 121,                   // 同上（别名）
    XEVENT_TYPE_APPLICATION_DEACTIVATE = 122,                  // 应用停用（已弃用）
    XEVENT_TYPE_APPLICATION_DEACTIVATED = 122,                 // 同上（别名）

    // WhatsThis mode
    XEVENT_TYPE_QUERY_WHATS_THIS = 123,                        // 查询“What's This”
    XEVENT_TYPE_ENTER_WHATS_THIS_MODE = 124,                   // 进入“What's This”模式
    XEVENT_TYPE_LEAVE_WHATS_THIS_MODE = 125,                   // 退出“What's This”模式

    // Z-order
    XEVENT_TYPE_Z_ORDER_CHANGE = 126,                          // Z 轴顺序变更

    // Hover
    XEVENT_TYPE_HOVER_ENTER = 127,                             // 悬停进入
    XEVENT_TYPE_HOVER_LEAVE = 128,                             // 悬停离开
    XEVENT_TYPE_HOVER_MOVE = 129,                              // 悬停移动

#ifdef QT_KEYPAD_NAVIGATION
    XEVENT_TYPE_ENTER_EDIT_FOCUS = 150,                        // 进入编辑焦点（键盘导航）
    XEVENT_TYPE_LEAVE_EDIT_FOCUS = 151,                        // 离开编辑焦点（键盘导航）
#endif
    XEVENT_TYPE_ACCEPT_DROPS_CHANGE = 152,                     // 接受拖拽状态变更
    
    XEVENT_TYPE_ZERO_TIMER_EVENT = 154,                        // 零间隔定时器事件
    
    // GraphicsView
    XEVENT_TYPE_GRAPHICS_SCENE_MOUSE_MOVE = 155,               // 图形场景鼠标移动
    XEVENT_TYPE_GRAPHICS_SCENE_MOUSE_PRESS = 156,              // 图形场景鼠标按下
    XEVENT_TYPE_GRAPHICS_SCENE_MOUSE_RELEASE = 157,            // 图形场景鼠标释放
    XEVENT_TYPE_GRAPHICS_SCENE_MOUSE_DOUBLE_CLICK = 158,       // 图形场景鼠标双击
    XEVENT_TYPE_GRAPHICS_SCENE_CONTEXT_MENU = 159,             // 图形场景上下文菜单
    XEVENT_TYPE_GRAPHICS_SCENE_HOVER_ENTER = 160,              // 图形场景悬停进入
    XEVENT_TYPE_GRAPHICS_SCENE_HOVER_MOVE = 161,               // 图形场景悬停移动
    XEVENT_TYPE_GRAPHICS_SCENE_HOVER_LEAVE = 162,              // 图形场景悬停离开
    XEVENT_TYPE_GRAPHICS_SCENE_HELP = 163,                     // 图形场景帮助
    XEVENT_TYPE_GRAPHICS_SCENE_DRAG_ENTER = 164,               // 图形场景拖拽进入
    XEVENT_TYPE_GRAPHICS_SCENE_DRAG_MOVE = 165,                // 图形场景拖拽移动
    XEVENT_TYPE_GRAPHICS_SCENE_DRAG_LEAVE = 166,               // 图形场景拖拽离开
    XEVENT_TYPE_GRAPHICS_SCENE_DROP = 167,                     // 图形场景拖拽放置
    XEVENT_TYPE_GRAPHICS_SCENE_WHEEL = 168,                    // 图形场景滚轮
    XEVENT_TYPE_GRAPHICS_SCENE_LEAVE = 220,                    // 图形场景离开
    
    XEVENT_TYPE_KEYBOARD_LAYOUT_CHANGE = 169,                  // 键盘布局变更
    XEVENT_TYPE_DYNAMIC_PROPERTY_CHANGE = 170,                 // 动态属性变更
    
    XEVENT_TYPE_TABLET_ENTER_PROXIMITY = 171,                  // 数位板笔进入感应区
    XEVENT_TYPE_TABLET_LEAVE_PROXIMITY = 172,                  // 数位板笔离开感应区
    
    XEVENT_TYPE_NON_CLIENT_AREA_MOUSE_MOVE = 173,              // 非客户区鼠标移动（如标题栏）
    XEVENT_TYPE_NON_CLIENT_AREA_MOUSE_BUTTON_PRESS = 174,      // 非客户区鼠标按下
    XEVENT_TYPE_NON_CLIENT_AREA_MOUSE_BUTTON_RELEASE = 175,    // 非客户区鼠标释放
    XEVENT_TYPE_NON_CLIENT_AREA_MOUSE_BUTTON_DBL_CLICK = 176,  // 非客户区鼠标双击
    
    XEVENT_TYPE_MAC_SIZE_CHANGE = 177,                         // macOS 窗口尺寸变更
    XEVENT_TYPE_CONTENTS_RECT_CHANGE = 178,                    // 内容矩形变更
    XEVENT_TYPE_MAC_GL_WINDOW_CHANGE = 179,                    // macOS OpenGL 窗口变更
    
    XEVENT_TYPE_FUTURE_CALL_OUT = 180,                         // 异步回调事件
    
    XEVENT_TYPE_GRAPHICS_SCENE_RESIZE = 181,                   // 图形场景调整大小
    XEVENT_TYPE_GRAPHICS_SCENE_MOVE = 182,                     // 图形场景移动
    
    XEVENT_TYPE_CURSOR_CHANGE = 183,                           // 光标形状变更
    XEVENT_TYPE_TOOL_TIP_CHANGE = 184,                         // 工具提示内容变更
    
    XEVENT_TYPE_NETWORK_REPLY_UPDATED = 185,                   // 网络响应更新
    
    XEVENT_TYPE_GRAB_MOUSE = 186,                              // 捕获鼠标
    XEVENT_TYPE_UNGRAB_MOUSE = 187,                            // 释放鼠标捕获
    XEVENT_TYPE_GRAB_KEYBOARD = 188,                           // 捕获键盘
    XEVENT_TYPE_UNGRAB_KEYBOARD = 189,                         // 释放键盘捕获
    
    XEVENT_TYPE_STATE_MACHINE_SIGNAL = 192,                    // 状态机动态信号
    XEVENT_TYPE_STATE_MACHINE_WRAPPED = 193,                   // 包装的状态机事件
    
    XEVENT_TYPE_TOUCH_BEGIN = 194,                             // 触摸开始
    XEVENT_TYPE_TOUCH_UPDATE = 195,                            // 触摸更新
    XEVENT_TYPE_TOUCH_END = 196,                               // 触摸结束

#ifndef QT_NO_GESTURES
    XEVENT_TYPE_NATIVE_GESTURE = 197,                          // 原生手势
    XEVENT_TYPE_GESTURE = 198,                                 // 手势事件
    XEVENT_TYPE_GESTURE_OVERRIDE = 202,                        // 手势覆盖
#endif

    XEVENT_TYPE_REQUEST_SOFTWARE_INPUT_PANEL = 199,            // 请求软键盘显示
    XEVENT_TYPE_CLOSE_SOFTWARE_INPUT_PANEL = 200,              // 请求软键盘关闭

    XEVENT_TYPE_WIN_ID_CHANGE = 203,                           // 窗口 ID 变更
    XEVENT_TYPE_SCROLL_PREPARE = 204,                          // 滚动准备
    XEVENT_TYPE_SCROLL = 205,                                  // 滚动事件

    XEVENT_TYPE_EXPOSE = 206,                                  // 暴露事件（窗口内容需重绘）
    XEVENT_TYPE_INPUT_METHOD_QUERY = 207,                      // 输入法查询
    XEVENT_TYPE_ORIENTATION_CHANGE = 208,                      // 屏幕方向变更
    XEVENT_TYPE_TOUCH_CANCEL = 209,                            // 触摸取消
    XEVENT_TYPE_THEME_CHANGE = 210,                            // 主题变更
    XEVENT_TYPE_SOCK_CLOSE = 211,                              // 套接字关闭
    XEVENT_TYPE_PLATFORM_PANEL = 212,                          // 平台面板事件（如虚拟键盘）
    XEVENT_TYPE_STYLE_ANIMATION_UPDATE = 213,                  // 样式动画更新
    XEVENT_TYPE_APPLICATION_STATE_CHANGE = 214,                // 应用状态变更（前台/后台）

    XEVENT_TYPE_WINDOW_CHANGE_INTERNAL = 215,                  // 内部窗口变更
    XEVENT_TYPE_SCREEN_CHANGE_INTERNAL = 216,                  // 内部屏幕变更
    XEVENT_TYPE_PLATFORM_SURFACE = 217,                        // 平台表面事件
    XEVENT_TYPE_POINTER = 218,                                 // 通用指针事件
    XEVENT_TYPE_TABLET_TRACKING_CHANGE = 219,                  // 数位板跟踪状态变更

    XEVENT_TYPE_WINDOW_ABOUT_TO_CHANGE_INTERNAL = 221,         // 内部窗口即将变更
    XEVENT_TYPE_DEVICE_PIXEL_RATIO_CHANGE = 222,               // 设备像素比变更

    XEVENT_TYPE_CHILD_WINDOW_ADDED = 223,                      // 子窗口添加
    XEVENT_TYPE_CHILD_WINDOW_REMOVED = 224,                    // 子窗口移除
    XEVENT_TYPE_PARENT_WINDOW_ABOUT_TO_CHANGE = 225,           // 父窗口即将变更
    XEVENT_TYPE_PARENT_WINDOW_CHANGE = 226,                    // 父窗口变更

    // Reserved for Qt Jambi
    // 512: MetaCall
    // 513: DeleteOnMainThread
    XEVENT_TYPE_FUNC_RUN,
    XEVENT_TYPE_USER = 1000,                                   // 用户自定义事件起始值
    XEVENT_TYPE_MAX_USER = 65535                               // 用户自定义事件最大值
} XEventType;
// 事件优先级
typedef enum {
    XEVENT_PRIORITY_LOWEST = 0,  // 最低优先级
    XEVENT_PRIORITY_LOW,         // 低优先级
    XEVENT_PRIORITY_NORMAL,      // 正常优先级
    XEVENT_PRIORITY_HIGH,        // 高优先级
    XEVENT_PRIORITY_HIGHEST,     // 最高优先级
    XEVENT_PRIORITY_COUNT        // 优先级数量（用于计数）
} XEventPriority;

#ifdef __cplusplus
}
#endif	
#endif