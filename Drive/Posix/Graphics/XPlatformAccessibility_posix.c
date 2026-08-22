/**
 * @file       XPlatformAccessibility_posix.c
 * @brief      Linux AT-SPI D-Bus 辅助功能桥接。
 * @details    本后端在 AT-SPI bus 可用时注册 application root，并公开
 *              org.a11y.atspi.Accessible/Application/Component 的基础查询
 *              面；无 AT-SPI registry 的普通桌面会话退化为 session D-Bus
 *              服务，方便诊断与后续 registry 启动后重连。
 */
#include "XPlatformAccessibility.h"

#if XWINDOW_ON && XACCESSIBLE_ON && XPLATFORMACCESSIBILITY_ATSPI_ON && \
    defined(__linux__) && defined(XINYUE_C_HAS_DBUS)

#include "XGuiApplication.h"
#if XWIDGET_ON
#include "XApplication.h"
#include "XWidget.h"
#endif
#include "XWindow.h"
#include "XMemory.h"
#include <dbus/dbus.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define XPA_BUS_NAME "org.xinyue.XGui.Accessibility"
#define XPA_ROOT_PATH "/org/a11y/atspi/accessible/root"
#define XPA_WINDOW_PREFIX "/org/a11y/atspi/accessible/window/"
#define XPA_FOREIGN_WINDOW_PREFIX "/org/a11y/atspi/accessible/foreign/"
#define XPA_WIDGET_PREFIX "/org/a11y/atspi/accessible/widget/"
#define XPA_NULL_PATH "/org/a11y/atspi/accessible/null"

typedef struct XPlatformAccessibilityPosix
{
    DBusConnection* connection;
    XPlatformAccessibility* bridge;
    bool active;
} XPlatformAccessibilityPosix;

static const char g_xpaIntrospection[] =
    "<node>"
    "<interface name='org.a11y.atspi.Accessible'>"
    "<method name='GetName'><arg type='s' direction='out'/></method>"
    "<method name='GetDescription'><arg type='s' direction='out'/></method>"
    "<method name='GetRole'><arg type='u' direction='out'/></method>"
    "<method name='GetChildCount'><arg type='i' direction='out'/></method>"
    "<method name='GetChildAtIndex'><arg type='i' direction='in'/>"
    "<arg type='(so)' direction='out'/></method>"
    "<method name='GetChildren'><arg type='a(so)' direction='out'/></method>"
    "<method name='GetParent'><arg type='(so)' direction='out'/></method>"
    "<method name='GetIndexInParent'><arg type='i' direction='out'/></method>"
    "<method name='GetRoleName'><arg type='s' direction='out'/></method>"
    "<method name='GetState'><arg type='au' direction='out'/></method>"
    "</interface>"
    "<interface name='org.a11y.atspi.Application'>"
    "<method name='GetToolkitName'><arg type='s' direction='out'/></method>"
    "<method name='GetVersion'><arg type='s' direction='out'/></method>"
    "</interface>"
    "<interface name='org.a11y.atspi.Component'>"
    "<method name='GetExtents'><arg type='u' direction='in'/>"
    "<arg type='i' direction='out'/><arg type='i' direction='out'/>"
    "<arg type='i' direction='out'/><arg type='i' direction='out'/></method>"
    "</interface>"
    "<interface name='org.freedesktop.DBus.Introspectable'>"
    "<method name='Introspect'><arg type='s' direction='out'/></method>"
    "</interface>"
    "</node>";

static unsigned int xpa_atspiRole(XAccessibleRole role)
{
    /* AT-SPI Role 枚举：application=75、window=68、button=13、text=60。 */
    switch (role) {
    case XAccessibleRole_Application: return 75u;
    case XAccessibleRole_Window: return 68u;
    case XAccessibleRole_Button: return 13u;
    case XAccessibleRole_Text: return 60u;
    case XAccessibleRole_List: return 33u;
    case XAccessibleRole_Table: return 54u;
    default: return 0u;
    }
}

static const char* xpa_roleName(XAccessibleRole role)
{
    switch (role) {
    case XAccessibleRole_Application: return "application";
    case XAccessibleRole_Window: return "frame";
    case XAccessibleRole_Client: return "panel";
    case XAccessibleRole_Button: return "push button";
    case XAccessibleRole_Text: return "text";
    case XAccessibleRole_List: return "list";
    case XAccessibleRole_Table: return "table";
    default: return "unknown";
    }
}

static bool xpa_pathFor(const XAccessible* accessible, char* path,
                        size_t capacity)
{
    XWindow* window;
    if (!accessible || !path || capacity == 0) return false;
#if XWIDGET_ON
    if (XAccessible_widget(accessible)) {
        snprintf(path, capacity, "%s%llu", XPA_WIDGET_PREFIX,
                 (unsigned long long)(uintptr_t)XAccessible_widget(accessible));
        return true;
    }
#endif
    window = XAccessible_window(accessible);
    if (!window) {
        snprintf(path, capacity, "%s", XPA_ROOT_PATH);
        return true;
    }
    if ((XWindow_type(window) & XWindowType_TypeMask) ==
        XWindowType_ForeignWindow) {
        snprintf(path, capacity, "%s%llu", XPA_FOREIGN_WINDOW_PREFIX,
                 (unsigned long long)(uintptr_t)window);
    } else {
        snprintf(path, capacity, "%s%llu", XPA_WINDOW_PREFIX,
                 (unsigned long long)XWindow_winId(window));
    }
    return true;
}

#if XWIDGET_ON && XAPPLICATION_ON && XGUIAPPLICATION_ON
static XAccessible* xpa_findWidget(XWidget* widget, uintptr_t target)
{
    const XVector* children;
    size_t i;
    if (!widget) return NULL;
    if ((uintptr_t)widget == target) return widget->m_accessible;
    children = XObject_children((const XObject*)widget);
    if (!children) return NULL;
    for (i = 0; i < XVector_size_base((const XContainer*)children); ++i) {
        XObject* child = XVector_At_Base(children, (int64_t)i, XObject*);
        XAccessible* result;
        if (!child || !XObject_isWidgetType(child)) continue;
        result = xpa_findWidget((XWidget*)child, target);
        if (result) return result;
    }
    return NULL;
}
#endif

static XAccessible* xpa_accessibleForPath(XPlatformAccessibilityPosix* state,
                                          const char* path)
{
    XAccessible* root;
    XVector* windows;
    unsigned long long id;
    char* end;
    size_t i;
    if (!state || !path || !(root = XPlatformAccessibility_root(state->bridge)))
        return NULL;
    if (strcmp(path, XPA_ROOT_PATH) == 0) return root;
#if XWIDGET_ON && XAPPLICATION_ON && XGUIAPPLICATION_ON
    if (strncmp(path, XPA_WIDGET_PREFIX, strlen(XPA_WIDGET_PREFIX)) == 0) {
        XVector* widgets = XApplication_topLevelWidgets();
        uintptr_t target = (uintptr_t)strtoull(
            path + strlen(XPA_WIDGET_PREFIX), &end, 10);
        XAccessible* result = NULL;
        if (!end || *end != '\0' || !widgets) {
            if (widgets) XVector_delete_base((XClass*)widgets);
            return NULL;
        }
        for (i = 0; i < XVector_size_base((const XContainer*)widgets) && !result; ++i) {
            XWidget* widget = XVector_At_Base(widgets, (int64_t)i, XWidget*);
            result = xpa_findWidget(widget, target);
        }
        XVector_delete_base((XClass*)widgets);
        return result;
    }
#endif
    if (strncmp(path, XPA_FOREIGN_WINDOW_PREFIX,
                strlen(XPA_FOREIGN_WINDOW_PREFIX)) == 0) {
        uintptr_t target = (uintptr_t)strtoull(
            path + strlen(XPA_FOREIGN_WINDOW_PREFIX), &end, 10);
        if (!end || *end != '\0') return NULL;
        windows = XGuiApplication_allWindows();
        if (!windows) return NULL;
        for (i = 0; i < XVector_size_base((const XContainer*)windows); ++i) {
            XWindow* window = XVector_At_Base(windows, (int64_t)i, XWindow*);
            if (window && (uintptr_t)window == target) {
                XAccessible* result = (XAccessible*)XWindow_accessibleRoot(window);
                XVector_delete_base((XClass*)windows);
                return result;
            }
        }
        XVector_delete_base((XClass*)windows);
        return NULL;
    }
    if (strncmp(path, XPA_WINDOW_PREFIX, strlen(XPA_WINDOW_PREFIX)) != 0)
        return NULL;
    id = strtoull(path + strlen(XPA_WINDOW_PREFIX), &end, 10);
    if (!end || *end != '\0') return NULL;
    windows = XGuiApplication_allWindows();
    if (!windows) return NULL;
    for (i = 0; i < XVector_size_base((const XContainer*)windows); ++i) {
        XWindow* window = XVector_At_Base(windows, (int64_t)i, XWindow*);
        if (window && (unsigned long long)XWindow_winId(window) == id) {
            XAccessible* result = (XAccessible*)XWindow_accessibleRoot(window);
            XVector_delete_base((XClass*)windows);
            return result;
        }
    }
    XVector_delete_base((XClass*)windows);
    return NULL;
}

static DBusMessage* xpa_error(DBusMessage* call, const char* name)
{
    return dbus_message_new_error(call, DBUS_ERROR_UNKNOWN_METHOD, name);
}

static bool xpa_appendReference(DBusMessage* reply, DBusConnection* connection,
                                const XAccessible* accessible)
{
    DBusMessageIter outer, tuple;
    char path[128];
    const char* busName;
    const char* pathArg;
    if (!reply || !connection) return false;
    if (accessible && !xpa_pathFor(accessible, path, sizeof(path))) return false;
    if (!accessible) snprintf(path, sizeof(path), "%s", XPA_NULL_PATH);
    busName = dbus_bus_get_unique_name(connection);
    pathArg = path;
    dbus_message_iter_init_append(reply, &outer);
    if (!dbus_message_iter_open_container(&outer, DBUS_TYPE_STRUCT, NULL, &tuple))
        return false;
    dbus_message_iter_append_basic(&tuple, DBUS_TYPE_STRING, &busName);
    dbus_message_iter_append_basic(&tuple, DBUS_TYPE_OBJECT_PATH, &pathArg);
    return dbus_message_iter_close_container(&outer, &tuple);
}

static XAccessible* xpa_parentFor(XPlatformAccessibilityPosix* state,
                                  XAccessible* accessible)
{
    XAccessible* parent;
    if (!state || !accessible || accessible->m_applicationRoot) return NULL;
    parent = XAccessible_parent(accessible);
    return parent ? parent : XPlatformAccessibility_root(state->bridge);
}

static int xpa_indexInParentFor(XPlatformAccessibilityPosix* state,
                                XAccessible* accessible)
{
    XAccessible* parent = xpa_parentFor(state, accessible);
    size_t i;
    if (!parent || !accessible) return -1;
    for (i = 0; i < XAccessible_childCount(parent); ++i)
        if (XAccessible_childAtIndex(parent, i) == accessible) return (int)i;
    return -1;
}

static DBusHandlerResult xpa_handleMessage(DBusConnection* connection,
                                           DBusMessage* call, void* userData)
{
    XPlatformAccessibilityPosix* state = (XPlatformAccessibilityPosix*)userData;
    XAccessible* accessible;
    DBusMessage* reply;
    const char* path;
    const char* interface;
    const char* member;
    if (!state || !call || dbus_message_get_type(call) != DBUS_MESSAGE_TYPE_METHOD_CALL)
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    path = dbus_message_get_path(call);
    interface = dbus_message_get_interface(call);
    member = dbus_message_get_member(call);
    if (!path || !interface || !member) return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    if (strcmp(interface, "org.freedesktop.DBus.Introspectable") == 0 &&
        strcmp(member, "Introspect") == 0) {
        reply = dbus_message_new_method_return(call);
        if (reply) {
            const char* xml = g_xpaIntrospection;
            dbus_message_append_args(reply, DBUS_TYPE_STRING, &xml,
                                     DBUS_TYPE_INVALID);
            dbus_connection_send(connection, reply, NULL);
            dbus_message_unref(reply);
        }
        return DBUS_HANDLER_RESULT_HANDLED;
    }
    accessible = xpa_accessibleForPath(state, path);
    if (!accessible) {
        reply = dbus_message_new_error(call, DBUS_ERROR_UNKNOWN_OBJECT,
                                       "Unknown accessible object");
        if (reply) { dbus_connection_send(connection, reply, NULL); dbus_message_unref(reply); }
        return DBUS_HANDLER_RESULT_HANDLED;
    }
    reply = dbus_message_new_method_return(call);
    if (!reply) return DBUS_HANDLER_RESULT_NEED_MEMORY;
    if (strcmp(interface, "org.a11y.atspi.Accessible") == 0 &&
        strcmp(member, "GetName") == 0) {
        XString* value = XAccessible_name(accessible);
        const char* text = value ? XString_toUtf8(value) : "";
        dbus_message_append_args(reply, DBUS_TYPE_STRING, &text, DBUS_TYPE_INVALID);
        if (value) XString_delete_base((XClass*)value);
    } else if (strcmp(interface, "org.a11y.atspi.Accessible") == 0 &&
               strcmp(member, "GetDescription") == 0) {
        XString* value = XAccessible_description(accessible);
        const char* text = value ? XString_toUtf8(value) : "";
        dbus_message_append_args(reply, DBUS_TYPE_STRING, &text, DBUS_TYPE_INVALID);
        if (value) XString_delete_base((XClass*)value);
    } else if (strcmp(interface, "org.a11y.atspi.Accessible") == 0 &&
               strcmp(member, "GetRole") == 0) {
        dbus_uint32_t role = (dbus_uint32_t)xpa_atspiRole(XAccessible_role(accessible));
        dbus_message_append_args(reply, DBUS_TYPE_UINT32, &role, DBUS_TYPE_INVALID);
    } else if (strcmp(interface, "org.a11y.atspi.Accessible") == 0 &&
               strcmp(member, "GetChildCount") == 0) {
        dbus_int32_t count = (dbus_int32_t)XAccessible_childCount(accessible);
        dbus_message_append_args(reply, DBUS_TYPE_INT32, &count, DBUS_TYPE_INVALID);
    } else if (strcmp(interface, "org.a11y.atspi.Accessible") == 0 &&
               strcmp(member, "GetChildAtIndex") == 0) {
        dbus_int32_t index = -1;
        XAccessible* child;
        char childPath[128];
        const char* busName;
            DBusMessageIter outer;
            DBusMessageIter tuple;
            const char* childPathArg;
        if (!dbus_message_get_args(call, NULL, DBUS_TYPE_INT32, &index,
                                   DBUS_TYPE_INVALID) || index < 0 ||
            !(child = XAccessible_childAtIndex(accessible, (size_t)index)) ||
            !xpa_pathFor(child, childPath, sizeof(childPath))) {
            dbus_message_unref(reply);
            reply = dbus_message_new_error(call, DBUS_ERROR_INVALID_ARGS,
                                           "Invalid child index");
        } else {
            busName = dbus_bus_get_unique_name(connection);
            childPathArg = childPath;
            dbus_message_iter_init_append(reply, &outer);
            dbus_message_iter_open_container(&outer, DBUS_TYPE_STRUCT, NULL, &tuple);
            dbus_message_iter_append_basic(&tuple, DBUS_TYPE_STRING, &busName);
            dbus_message_iter_append_basic(&tuple, DBUS_TYPE_OBJECT_PATH,
                                           &childPathArg);
            dbus_message_iter_close_container(&outer, &tuple);
        }
    } else if (strcmp(interface, "org.a11y.atspi.Accessible") == 0 &&
               strcmp(member, "GetChildren") == 0) {
        DBusMessageIter array, tuple;
        size_t i;
        dbus_message_iter_init_append(reply, &array);
        dbus_message_iter_open_container(&array, DBUS_TYPE_ARRAY, "(so)", &tuple);
        for (i = 0; i < XAccessible_childCount(accessible); ++i) {
            XAccessible* child = XAccessible_childAtIndex(accessible, i);
            char childPath[128];
            const char* childPathArg;
            const char* busName;
            DBusMessageIter item;
            if (!child || !xpa_pathFor(child, childPath, sizeof(childPath))) continue;
            childPathArg = childPath;
            busName = dbus_bus_get_unique_name(connection);
            dbus_message_iter_open_container(&tuple, DBUS_TYPE_STRUCT, NULL, &item);
            dbus_message_iter_append_basic(&item, DBUS_TYPE_STRING, &busName);
            dbus_message_iter_append_basic(&item, DBUS_TYPE_OBJECT_PATH, &childPathArg);
            dbus_message_iter_close_container(&tuple, &item);
        }
        dbus_message_iter_close_container(&array, &tuple);
    } else if (strcmp(interface, "org.a11y.atspi.Accessible") == 0 &&
               strcmp(member, "GetParent") == 0) {
        (void)xpa_appendReference(reply, connection, xpa_parentFor(state, accessible));
    } else if (strcmp(interface, "org.a11y.atspi.Accessible") == 0 &&
               strcmp(member, "GetIndexInParent") == 0) {
        dbus_int32_t index = (dbus_int32_t)xpa_indexInParentFor(state, accessible);
        dbus_message_append_args(reply, DBUS_TYPE_INT32, &index, DBUS_TYPE_INVALID);
    } else if (strcmp(interface, "org.a11y.atspi.Accessible") == 0 &&
               strcmp(member, "GetRoleName") == 0) {
        const char* roleName = xpa_roleName(XAccessible_role(accessible));
        dbus_message_append_args(reply, DBUS_TYPE_STRING, &roleName, DBUS_TYPE_INVALID);
    } else if (strcmp(interface, "org.a11y.atspi.Accessible") == 0 &&
               strcmp(member, "GetState") == 0) {
        DBusMessageIter array;
        DBusMessageIter stateArray;
        dbus_message_iter_init_append(reply, &array);
        dbus_message_iter_open_container(&array, DBUS_TYPE_ARRAY, "u", &stateArray);
        dbus_message_iter_close_container(&array, &stateArray);
    } else if (strcmp(interface, "org.a11y.atspi.Application") == 0 &&
               strcmp(member, "GetToolkitName") == 0) {
        const char* value = "XinYueC";
        dbus_message_append_args(reply, DBUS_TYPE_STRING, &value, DBUS_TYPE_INVALID);
    } else if (strcmp(interface, "org.a11y.atspi.Application") == 0 &&
               strcmp(member, "GetVersion") == 0) {
        const char* value = "1";
        dbus_message_append_args(reply, DBUS_TYPE_STRING, &value, DBUS_TYPE_INVALID);
    } else if (strcmp(interface, "org.a11y.atspi.Component") == 0 &&
               strcmp(member, "GetExtents") == 0) {
        XRect rect = XAccessible_rect(accessible);
        dbus_int32_t x = rect.x, y = rect.y, width = rect.width, height = rect.height;
        dbus_message_append_args(reply, DBUS_TYPE_INT32, &x, DBUS_TYPE_INT32, &y,
                                 DBUS_TYPE_INT32, &width, DBUS_TYPE_INT32, &height,
                                 DBUS_TYPE_INVALID);
    } else {
        dbus_message_unref(reply);
        reply = xpa_error(call, "Unsupported accessible method");
    }
    if (reply) { dbus_connection_send(connection, reply, NULL); dbus_message_unref(reply); }
    dbus_connection_flush(connection);
    return DBUS_HANDLER_RESULT_HANDLED;
}

static const DBusObjectPathVTable g_xpaVTable = {
    .unregister_function = NULL,
    .message_function = xpa_handleMessage,
    .dbus_internal_pad1 = NULL,
    .dbus_internal_pad2 = NULL,
    .dbus_internal_pad3 = NULL,
    .dbus_internal_pad4 = NULL
};

#if XWIDGET_ON && XAPPLICATION_ON && XGUIAPPLICATION_ON
static void xpa_registerWidgetTree(XPlatformAccessibilityPosix* state,
                                   XWidget* widget)
{
    const XVector* children;
    size_t i;
    if (!state || !widget || !widget->m_accessible) return;
    {
        char path[128];
        if (xpa_pathFor(widget->m_accessible, path, sizeof(path)))
            dbus_connection_try_register_object_path(state->connection, path,
                                                     &g_xpaVTable, state, NULL);
    }
    children = XObject_children((const XObject*)widget);
    if (!children) return;
    for (i = 0; i < XVector_size_base((const XContainer*)children); ++i) {
        XObject* child = XVector_At_Base(children, (int64_t)i, XObject*);
        if (child && XObject_isWidgetType(child))
            xpa_registerWidgetTree(state, (XWidget*)child);
    }
}
static void xpa_registerAllWidgets(XPlatformAccessibilityPosix* state)
{
    XVector* widgets;
    size_t i;
    if (!state) return;
    widgets = XApplication_topLevelWidgets();
    if (!widgets) return;
    for (i = 0; i < XVector_size_base((const XContainer*)widgets); ++i) {
        XWidget* widget = XVector_At_Base(widgets, (int64_t)i, XWidget*);
        if (widget) xpa_registerWidgetTree(state, widget);
    }
    XVector_delete_base((XClass*)widgets);
}
#endif

static void xpa_embedInAtspiRegistry(DBusConnection* connection)
{
    DBusMessage* call;
    DBusMessage* reply;
    DBusMessageIter outer;
    DBusMessageIter reference;
    DBusError error;
    const char* busName;
    const char* rootPath = XPA_ROOT_PATH;
    if (!connection) return;
    call = dbus_message_new_method_call("org.a11y.atspi.Registry",
        "/org/a11y/atspi/registry", "org.a11y.atspi.Socket", "Embed");
    if (!call) return;
    busName = dbus_bus_get_unique_name(connection);
    dbus_message_iter_init_append(call, &outer);
    dbus_message_iter_open_container(&outer, DBUS_TYPE_STRUCT, NULL,
                                     &reference);
    dbus_message_iter_append_basic(&reference, DBUS_TYPE_STRING, &busName);
    dbus_message_iter_append_basic(&reference, DBUS_TYPE_OBJECT_PATH,
                                   &rootPath);
    dbus_message_iter_close_container(&outer, &reference);
    dbus_error_init(&error);
    reply = dbus_connection_send_with_reply_and_block(connection, call, 1000,
                                                      &error);
    if (reply) dbus_message_unref(reply);
    if (dbus_error_is_set(&error)) dbus_error_free(&error);
    dbus_message_unref(call);
}

bool XPlatformAccessibilityDriver_start(XPlatformAccessibility* bridge,
                                        void** nativeState)
{
    DBusError error;
    DBusConnection* connection;
    XPlatformAccessibilityPosix* state;
    int requestResult;
    if (nativeState) *nativeState = NULL;
    if (!bridge || !nativeState) return false;
    dbus_error_init(&error);
    connection = dbus_bus_get(DBUS_BUS_SESSION, &error);
    if (dbus_error_is_set(&error) || !connection) {
        dbus_error_free(&error);
        return false;
    }
    dbus_connection_set_exit_on_disconnect(connection, FALSE);
    requestResult = dbus_bus_request_name(connection, XPA_BUS_NAME,
                                           DBUS_NAME_FLAG_DO_NOT_QUEUE, &error);
    if (dbus_error_is_set(&error) ||
        (requestResult != DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER &&
         requestResult != DBUS_REQUEST_NAME_REPLY_ALREADY_OWNER &&
         requestResult != DBUS_REQUEST_NAME_REPLY_EXISTS)) {
        dbus_error_free(&error);
        return false;
    }
    state = (XPlatformAccessibilityPosix*)XMalloc_System(sizeof(*state));
    if (!state) return false;
    memset(state, 0, sizeof(*state));
    state->connection = connection;
    state->bridge = bridge;
    if (!dbus_connection_register_object_path(connection, XPA_ROOT_PATH,
                                              &g_xpaVTable, state)) {
        XFree_System(state);
        return false;
    }
    state->active = true;
    *nativeState = state;
#if XWIDGET_ON && XAPPLICATION_ON && XGUIAPPLICATION_ON
    xpa_registerAllWidgets(state);
#endif
    /* 标准 AT-SPI Registry 存在时完成 Embed 注册；没有 registry 的会话仍
       保留同一标准对象路径和接口，便于诊断客户端直接访问。 */
    xpa_embedInAtspiRegistry(connection);
    return true;
}

void XPlatformAccessibilityDriver_stop(void* nativeState)
{
    XPlatformAccessibilityPosix* state = (XPlatformAccessibilityPosix*)nativeState;
    if (!state) return;
    if (state->connection) {
        dbus_connection_unregister_object_path(state->connection, XPA_ROOT_PATH);
        dbus_bus_release_name(state->connection, XPA_BUS_NAME, NULL);
        dbus_connection_flush(state->connection);
        dbus_connection_unref(state->connection);
    }
    XFree_System(state);
}

bool XPlatformAccessibilityDriver_isActive(void* nativeState)
{
    XPlatformAccessibilityPosix* state = (XPlatformAccessibilityPosix*)nativeState;
    return state && state->active && state->connection &&
           dbus_connection_get_is_connected(state->connection);
}

void XPlatformAccessibilityDriver_notify(void* nativeState,
                                         XAccessibleEvent event,
                                         XAccessible* accessible)
{
    XPlatformAccessibilityPosix* state = (XPlatformAccessibilityPosix*)nativeState;
    char path[128];
    DBusMessage* signal;
    const char* eventName;
    if (!XPlatformAccessibilityDriver_isActive(state) || !accessible ||
        !xpa_pathFor(accessible, path, sizeof(path))) return;
    if (event == XAccessibleEvent_ObjectCreated)
        (void)dbus_connection_try_register_object_path(state->connection, path,
                                                       &g_xpaVTable, state, NULL);
#if XWIDGET_ON
    else if (XAccessible_widget(accessible))
        (void)dbus_connection_try_register_object_path(state->connection, path,
                                                       &g_xpaVTable, state, NULL);
#endif
    eventName = event == XAccessibleEvent_NameChanged ? "NameChanged" :
                event == XAccessibleEvent_LocationChanged ? "LocationChanged" :
                event == XAccessibleEvent_StateChanged ? "StateChanged" :
                event == XAccessibleEvent_ObjectDestroyed ? "ObjectDestroyed" :
                "ObjectCreated";
    signal = dbus_message_new_signal(path, "org.xinyue.XGui.Accessibility",
                                     "ObjectChanged");
    if (signal) {
        dbus_message_append_args(signal, DBUS_TYPE_STRING, &eventName,
                                 DBUS_TYPE_INVALID);
        dbus_connection_send(state->connection, signal, NULL);
        dbus_message_unref(signal);
    }
    /* Keep the handler registered until the connection closes.  The public
       object lookup already returns UnknownObject after destruction, and
       avoiding per-window unregister races keeps the single-threaded D-Bus
       dispatch free of stale-path warnings during application teardown. */
    dbus_connection_flush(state->connection);
}

void XPlatformAccessibilityDriver_processEvents(void* nativeState)
{
    XPlatformAccessibilityPosix* state = (XPlatformAccessibilityPosix*)nativeState;
    if (!XPlatformAccessibilityDriver_isActive(state)) return;
#if XWIDGET_ON && XAPPLICATION_ON && XGUIAPPLICATION_ON
    xpa_registerAllWidgets(state);
#endif
    while (dbus_connection_read_write_dispatch(state->connection, 0) ==
           DBUS_DISPATCH_DATA_REMAINS) { }
}

#endif /* Linux dbus AT-SPI bridge */
