#include"XPropertyTest.h"
#if DEMOTEST && XPROPERTY_ON

#include"XProperty.h"
#include"XPropertyBinding.h"
#include"XPropertyObserver.h"
#include"XBindable.h"
#include"XPropertyAlias.h"
#include"XObjectBindableProperty.h"
#include"XObject.h"
#include"XThread.h"
#include"XAtomic.h"
#include"XCoreApplication.h"
#include<string.h>
#include"XTestMenu.h"
#include"XAction.h"
#include"XPrintf.h"

// ==================== 测试辅助 ====================
#define TEST_PASS(name) XPrintf("[PASS] %s\n", name)
#define TEST_FAIL(name, reason) XPrintf("[FAIL] %s: %s\n", name, reason)
#define TEST_INFO(...) XPrintf_line("[INFO] ", __VA_ARGS__)

/* 前向声明（处理器函数定义在后） */
static bool count_handler(void* user, const XVariant* newValue);
static bool handler_once(void* user, const XVariant* newValue);

/* 生成 Int32 变体（堆） */
static XVariant* make_int(int32_t v)
{
	return XVariant_create_int32(v);
}

/* 校验变体为指定 Int32 值 */
static bool is_int(const XVariant* v, int32_t expect)
{
	return v && XVariant_type((XVariant*)v) == XVariantType_Int32 && XVariant_toInt32(v) == expect;
}

/* ==================== 演示对象：嵌入 XObjectBindableProperty 并声明 notify 信号 ==================== */

XCLASS_DEFINE_BEGING(XTestPropObject)
XCLASS_DEFINE_EXTEND_END(XTestPropObject, XObject)

typedef struct XTestPropObject
{
	XObject m_class;                  /**< 基类成员；必须是第一个。 */
	XObjectBindableProperty m_value;  /**< 演示用的对象绑定属性。 */
} XTestPropObject;

/* notify 信号前向声明（init 中经 XSignal 取标识） */
static void* XTestPropObject_valueChanged_signal(XTestPropObject* self);

static void VXTestPropObject_deinit(XTestPropObject* obj)
{
	if (!obj) return;
	XObjectBindableProperty_deinit(&obj->m_value);
	XClass_Deinit_Parent(XObject, (XObject*)obj);
}

XVtable* XTestPropObject_class_init(void)
{
	XVTABLE_INIT_DEFAULT(XTestPropObject)
	XVTABLE_INHERIT_XCLASS(XObject);
	XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXTestPropObject_deinit);
	return XVTABLE_DEFAULT;
}

static void XTestPropObject_init(XTestPropObject* self)
{
	if (!self) return;
	memset(self, 0, sizeof(XTestPropObject));
	XObject_init(self);
	XClassSetVtable(self, XTestPropObject);
	/* notify 信号 = 自身声明的 valueChanged；对标 QObjectBindableProperty(owner, signal) */
	XObjectBindableProperty_init(&self->m_value, (XObject*)self,
		(size_t)XSignal(XTestPropObject_valueChanged_signal));
}

static XTestPropObject* XTestPropObject_create(void)
{
	XTestPropObject* self = (XTestPropObject*)XMemory_malloc(sizeof(XTestPropObject), XCLASS_DEFAULT_MEMORY_TYPE);
	if (!self) return NULL;
	memset(self, 0, sizeof(XTestPropObject));
	XTestPropObject_init(self);
	Set_Class_Memory(self, XCLASS_DEFAULT_MEMORY_TYPE);
	Set_Class_IsHeap(self, true);
	return self;
}

/** notify 信号（无参，对标 changed() 通知信号）。 */
static void* XTestPropObject_valueChanged_signal(XTestPropObject* self)
{
	XEmitSignal(self, XTestPropObject_valueChanged_signal, NULL, NULL, NULL, XEVENT_PRIORITY_NORMAL);
}

/* ==================== 测试 1：基础读写 ==================== */

static bool test_basic(void)
{
	TEST_INFO("===== 基础读写 =====");
	bool pass = false;
	XProperty* p = XProperty_create();
	if (!p) { TEST_FAIL("创建属性", "返回NULL"); return false; }
	{
		XVariant* v = make_int(42);
		if (!v) { TEST_FAIL("创建变体", "返回NULL"); goto cleanup; }
		XProperty_setValue(p, v);
		XVariant_delete_base((XClass*)v);
	}
	if (!is_int(XProperty_value_const(p), 42)) { TEST_FAIL("读取属性", "值不等于42"); goto cleanup; }

	/* 拷贝读取 */
	{
		XVariant out;
		memset(&out, 0, sizeof(XVariant));
		XVariant_init(&out, NULL, 0, XVariantType_NULL);
		if (!XProperty_value(p, &out) || !is_int(&out, 42)) { XVariant_deinit_base((XClass*)&out); TEST_FAIL("拷贝读取", "失败"); goto cleanup; }
		XVariant_deinit_base((XClass*)&out);
	}

	/* 拷贝/移动语义 */
	{
		XProperty* copy = XProperty_create_copy(p);
		if (!copy || !is_int(XProperty_value_const(copy), 42)) { if (copy) XProperty_delete_base((XClass*)copy); TEST_FAIL("拷贝创建", "失败"); goto cleanup; }
		XProperty_delete_base((XClass*)copy);

		XProperty moved;
		XProperty_init(&moved);
		XMove((XClass*)&moved, (XClass*)p);
		if (!is_int(XProperty_value_const(&moved), 42)) { XProperty_deinit_base((XClass*)&moved); TEST_FAIL("移动语义", "值丢失"); goto cleanup; }
		XProperty_deinit_base((XClass*)&moved);
	}
	pass = true;
cleanup:
	XProperty_delete_base((XClass*)p);
	if (pass) TEST_PASS("基础读写");
	return pass;
}

/* ==================== 测试 2：绑定惰性求值与依赖捕获 ==================== */

typedef struct SumCtx { XProperty* a; XProperty* b; } SumCtx;

static bool eval_sum(void* user, XVariant* out)
{
	SumCtx* ctx = (SumCtx*)user;
	int32_t sum = XVariant_toInt32(XProperty_value_const(ctx->a))
		+ XVariant_toInt32(XProperty_value_const(ctx->b));
	XVariant_init(out, &sum, sizeof(sum), XVariantType_Int32);
	return true;
}

static bool test_binding_eval(void)
{
	TEST_INFO("===== 绑定求值与依赖捕获 =====");
	bool pass = false;
	XProperty *a = XProperty_create(), *b = XProperty_create(), *c = XProperty_create();
	SumCtx ctx;
	if (!a || !b || !c) { TEST_FAIL("创建属性", "返回NULL"); goto cleanup; }
	ctx.a = a; ctx.b = b;
	{
		XVariant* va = make_int(1); XProperty_setValue(a, va); XVariant_delete_base((XClass*)va);
		XVariant* vb = make_int(2); XProperty_setValue(b, vb); XVariant_delete_base((XClass*)vb);
	}
	if (!XProperty_setBinding_eval(c, eval_sum, &ctx, NULL)) { TEST_FAIL("设置绑定", "失败"); goto cleanup; }
	if (!XProperty_hasBinding(c)) { TEST_FAIL("hasBinding", "应为true"); goto cleanup; }
	/* 惰性：设置绑定后未读取前不应有值产生（初值仍为 NULL 变体） */
	if (XVariant_type((XVariant*)XProperty_valueBypassingBindings_const(c)) != XVariantType_NULL)
	{ TEST_FAIL("惰性求值", "设置绑定即求值"); goto cleanup; }
	if (!is_int(XProperty_value_const(c), 3)) { TEST_FAIL("首次求值", "1+2!=3"); goto cleanup; }

	/* 依赖已捕获：改 a 后重读 c 自动更新 */
	{
		XVariant* va = make_int(10); XProperty_setValue(a, va); XVariant_delete_base((XClass*)va);
	}
	if (!is_int(XProperty_value_const(c), 12)) { TEST_FAIL("依赖更新", "10+2!=12"); goto cleanup; }
	pass = true;
cleanup:
	if (a) XProperty_delete_base((XClass*)a);
	if (b) XProperty_delete_base((XClass*)b);
	if (c) XProperty_delete_base((XClass*)c);
	if (pass) TEST_PASS("绑定求值与依赖捕获");
	return pass;
}

/* ==================== 测试 3：级联通知（观察者） ==================== */

static bool test_cascade_notify(void)
{
	TEST_INFO("===== 级联通知 =====");
	bool pass = false;
	int fired = 0;
	XProperty *a = XProperty_create(), *c = XProperty_create();
	SumCtx ctx;
	XPropertyObserver* obs = NULL;
	if (!a || !c) { TEST_FAIL("创建属性", "返回NULL"); goto cleanup; }
	ctx.a = a; ctx.b = a;   /* c = a + a */
	{
		XVariant* va = make_int(1); XProperty_setValue(a, va); XVariant_delete_base((XClass*)va);
	}
	if (!XProperty_setBinding_eval(c, eval_sum, &ctx, NULL)) { TEST_FAIL("设置绑定", "失败"); goto cleanup; }
	(void)XProperty_value_const(c);   /* 首次求值：c=2 */

	/* 用 ChangeHandler 计数（回调签名带 user 指针） */
	obs = XPropertyObserver_create();
	if (!obs) { TEST_FAIL("创建观察者", "返回NULL"); goto cleanup; }
	XPropertyObserver_setHandler(obs, count_handler, &fired);
	if (!XProperty_subscribe(c, obs)) { TEST_FAIL("订阅", "失败"); goto cleanup; }
	{
		/* 改 a：级联把 c 标脏并立即通知 c 的观察者（对标 Qt 默认 Asynchronous 语义） */
		XVariant* va = make_int(5); XProperty_setValue(a, va); XVariant_delete_base((XClass*)va);
	}
	if (fired < 1) { TEST_FAIL("级联通知", "依赖变化未通知 c 的观察者"); goto cleanup; }
	/* 读取 c 触发重算写入：值实际变化，观察者再次收到（对标 Qt 双段通知） */
	if (!is_int(XProperty_value_const(c), 10)) { TEST_FAIL("重算", "5+5!=10"); goto cleanup; }
	pass = true;
cleanup:
	if (obs) XPropertyObserver_delete(obs);
	if (a) XProperty_delete_base((XClass*)a);
	if (c) XProperty_delete_base((XClass*)c);
	if (pass) TEST_PASS("级联通知");
	return pass;
}

/* ChangeHandler：计数后继续订阅 */
static bool count_handler(void* user, const XVariant* newValue)
{
	(void)newValue;
	(*(int*)user)++;
	return false;
}

/* ==================== 测试 4：动态依赖重捕获 ==================== */

typedef struct CondCtx { XProperty* cond; XProperty* src; } CondCtx;

static bool eval_conditional(void* user, XVariant* out)
{
	CondCtx* ctx = (CondCtx*)user;
	int32_t result;
	if (XVariant_toBool(XProperty_value_const(ctx->cond)))
		result = XVariant_toInt32(XProperty_value_const(ctx->src));
	else
		result = -1;
	XVariant_init(out, &result, sizeof(result), XVariantType_Int32);
	return true;
}

static bool test_dynamic_dependency(void)
{
	TEST_INFO("===== 动态依赖重捕获 =====");
	bool pass = false;
	int fired = 0;
	XProperty *cond = XProperty_create(), *src = XProperty_create(), *c = XProperty_create();
	CondCtx ctx;
	XPropertyObserver* obs = NULL;
	if (!cond || !src || !c) { TEST_FAIL("创建属性", "返回NULL"); goto cleanup; }
	ctx.cond = cond; ctx.src = src;
	{
		XVariant* vt = XVariant_create_bool(true); XProperty_setValue(cond, vt); XVariant_delete_base((XClass*)vt);
		XVariant* vs = make_int(7); XProperty_setValue(src, vs); XVariant_delete_base((XClass*)vs);
	}
	if (!XProperty_setBinding_eval(c, eval_conditional, &ctx, NULL)) { TEST_FAIL("设置绑定", "失败"); goto cleanup; }
	if (!is_int(XProperty_value_const(c), 7)) { TEST_FAIL("条件求值", "cond=true 应取 src"); goto cleanup; }

	obs = XPropertyObserver_create();
	if (!obs) { TEST_FAIL("创建观察者", "返回NULL"); goto cleanup; }
	XPropertyObserver_setHandler(obs, count_handler, &fired);
	XProperty_subscribe(c, obs);

	/* cond 变 false：cond 与 src 都是依赖，src 变化会通知 c */
	{
		XVariant* vf = XVariant_create_bool(false); XProperty_setValue(cond, vf); XVariant_delete_base((XClass*)vf);
	}
	(void)XProperty_value_const(c);   /* 重算：c=-1，此后依赖仅剩 cond */
	{
		int before = fired;
		XVariant* vs = make_int(99); XProperty_setValue(src, vs); XVariant_delete_base((XClass*)vs);
		if (fired != before) { TEST_FAIL("动态依赖", "src 已不再是依赖，不应通知 c"); goto cleanup; }
	}
	if (!is_int(XProperty_value_const(c), -1)) { TEST_FAIL("条件求值", "cond=false 应为-1"); goto cleanup; }
	pass = true;
cleanup:
	if (obs) XPropertyObserver_delete(obs);
	if (cond) XProperty_delete_base((XClass*)cond);
	if (src) XProperty_delete_base((XClass*)src);
	if (c) XProperty_delete_base((XClass*)c);
	if (pass) TEST_PASS("动态依赖重捕获");
	return pass;
}

/* ==================== 测试 5：绑定循环检测 ==================== */

static bool eval_self(void* user, XVariant* out)
{
	XProperty* p = (XProperty*)user;
	const XVariant* v = XProperty_value_const(p);   /* 读取自身 → 循环 */
	if (v && v->m_data)
		XVariant_init(out, v->m_data, v->m_dataSize, v->m_type);
	return true;
}

static bool test_binding_loop(void)
{
	TEST_INFO("===== 绑定循环检测 =====");
	bool pass = false;
	XProperty* p = XProperty_create();
	if (!p) { TEST_FAIL("创建属性", "返回NULL"); goto cleanup; }
	{
		XVariant* v = make_int(9); XProperty_setValue(p, v); XVariant_delete_base((XClass*)v);
	}
	/* 绑定求值中读取自身目标属性：BindingLoop，返回现值，不重入不崩溃 */
	(void)XProperty_setBinding_eval(p, eval_self, p, NULL);
	(void)XProperty_value_const(p);
	{
		const XPropertyBinding* b = XProperty_binding_const(p);
		const XPropertyBindingError* err = b ? XPropertyBinding_error_const(b) : NULL;
		if (!err || XPropertyBindingError_type(err) != XPropertyBindingError_BindingLoop)
		{ TEST_FAIL("循环检测", "未记录 BindingLoop 错误"); goto cleanup; }
	}
	if (!is_int(XProperty_value_const(p), 9)) { TEST_FAIL("循环回退", "现值应保持9"); goto cleanup; }
	pass = true;
cleanup:
	if (p) XProperty_delete_base((XClass*)p);
	if (pass) TEST_PASS("绑定循环检测");
	return pass;
}

/* ==================== 测试 6：显式赋值断开绑定 / takeBinding ==================== */

static bool test_binding_break(void)
{
	TEST_INFO("===== 赋值断开与takeBinding =====");
	bool pass = false;
	XProperty *a = XProperty_create(), *c = XProperty_create();
	SumCtx ctx;
	if (!a || !c) { TEST_FAIL("创建属性", "返回NULL"); goto cleanup; }
	ctx.a = a; ctx.b = a;
	{
		XVariant* va = make_int(1); XProperty_setValue(a, va); XVariant_delete_base((XClass*)va);
	}
	XProperty_setBinding_eval(c, eval_sum, &ctx, NULL);
	(void)XProperty_value_const(c);   /* c=2 */
	{
		/* 显式赋值断开绑定（与值是否相等无关，对标 Qt） */
		XVariant* v = make_int(999); XProperty_setValue(c, v); XVariant_delete_base((XClass*)v);
	}
	if (XProperty_hasBinding(c)) { TEST_FAIL("赋值断开", "hasBinding 应为 false"); goto cleanup; }
	{
		XVariant* va = make_int(100); XProperty_setValue(a, va); XVariant_delete_base((XClass*)va);
	}
	if (!is_int(XProperty_value_const(c), 999)) { TEST_FAIL("断开后独立", "c 不应再随 a 变化"); goto cleanup; }

	/* takeBinding：所有权移交调用方 */
	{
		XProperty* d = XProperty_create();
		XPropertyBinding* taken = NULL;
		if (!d) { TEST_FAIL("创建属性", "返回NULL"); goto cleanup; }
		XProperty_setBinding_eval(d, eval_sum, &ctx, NULL);
		taken = XProperty_takeBinding(d);
		if (!taken || XProperty_hasBinding(d)) { if (taken) XPropertyBinding_unref(taken); XProperty_delete_base((XClass*)d); TEST_FAIL("takeBinding", "应取出并解除"); goto cleanup; }
		if (!XPropertyBinding_isValid(taken)) { XPropertyBinding_unref(taken); XProperty_delete_base((XClass*)d); TEST_FAIL("takeBinding", "取出的绑定无效"); goto cleanup; }
		XPropertyBinding_unref(taken);
		XProperty_delete_base((XClass*)d);
	}
	pass = true;
cleanup:
	if (a) XProperty_delete_base((XClass*)a);
	if (c) XProperty_delete_base((XClass*)c);
	if (pass) TEST_PASS("赋值断开与takeBinding");
	return pass;
}

/* ==================== 测试 7：别名 ==================== */

static bool test_alias(void)
{
	TEST_INFO("===== 属性别名 =====");
	bool pass = false;
	int fired = 0;
	XProperty* a = XProperty_create();
	XPropertyAlias alias, aliasOfAlias;
	XPropertyObserver* obs = NULL;
	if (!a) { TEST_FAIL("创建属性", "返回NULL"); goto cleanup; }
	{
		XVariant* va = make_int(10); XProperty_setValue(a, va); XVariant_delete_base((XClass*)va);
	}
	XPropertyAlias_init(&alias);
	XPropertyAlias_init(&aliasOfAlias);
	if (!XPropertyAlias_init_ex_2(&alias, a)) { TEST_FAIL("别名属性", "失败"); goto cleanup; }
	if (!XPropertyAlias_init_ex_4(&aliasOfAlias, &alias)) { TEST_FAIL("别名的别名", "失败"); goto cleanup; }
	if (!XPropertyAlias_isValid(&alias) || !XPropertyAlias_isValid(&aliasOfAlias)) { TEST_FAIL("别名有效", "失败"); goto cleanup; }
	if (!is_int(XPropertyAlias_value_const(&alias), 10) || !is_int(XPropertyAlias_value_const(&aliasOfAlias), 10))
	{ TEST_FAIL("别名读取", "值不等于10"); goto cleanup; }

	/* 别名写入穿透到源属性 */
	{
		XVariant* v = make_int(20); XPropertyAlias_setValue(&alias, v); XVariant_delete_base((XClass*)v);
	}
	if (!is_int(XProperty_value_const(a), 20)) { TEST_FAIL("别名写入", "源属性应为20"); goto cleanup; }
	{
		XVariant* v = make_int(30); XPropertyAlias_setValue(&aliasOfAlias, v); XVariant_delete_base((XClass*)v);
	}
	if (!is_int(XProperty_value_const(a), 30)) { TEST_FAIL("别名链写入", "源属性应为30"); goto cleanup; }

	/* 经别名订阅源属性 */
	obs = XPropertyObserver_create();
	if (!obs) { TEST_FAIL("创建观察者", "返回NULL"); goto cleanup; }
	XPropertyObserver_setHandler(obs, count_handler, &fired);
	if (!XPropertyAlias_subscribe(&alias, obs)) { TEST_FAIL("别名订阅", "失败"); goto cleanup; }
	{
		XVariant* v = make_int(40); XProperty_setValue(a, v); XVariant_delete_base((XClass*)v);
	}
	if (fired != 1) { TEST_FAIL("别名订阅", "源属性变化未通知"); goto cleanup; }
	pass = true;
cleanup:
	if (obs) XPropertyObserver_delete(obs);
	XPropertyAlias_deinit(&alias);
	XPropertyAlias_deinit(&aliasOfAlias);
	if (a) XProperty_delete_base((XClass*)a);
	if (pass) TEST_PASS("属性别名");
	return pass;
}

/* ==================== 测试 8：对象绑定属性 notify 信号 ==================== */

static int s_signalCount = 0;

static void value_changed_slot(XObject* receiver, XVarList* args)
{
	(void)receiver; (void)args;
	s_signalCount++;
}

static bool test_object_property(void)
{
	TEST_INFO("===== 对象绑定属性 notify 信号 =====");
	bool pass = false;
	XTestPropObject* obj = XTestPropObject_create();
	XObject* receiver = XObject_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
	XProperty* src = XProperty_create();
	if (!obj || !receiver || !src) { TEST_FAIL("创建对象", "返回NULL"); goto cleanup; }
	s_signalCount = 0;

	/* 直写：值变化 → notify 信号 → 槽收到 */
	if (!XObject_connect_1((XObject*)obj, XSignal(XTestPropObject_valueChanged_signal),
		receiver, value_changed_slot, XConnectionType_Direct))
	{ TEST_FAIL("连接信号", "失败"); goto cleanup; }
	{
		XVariant* v = make_int(5); XObjectBindableProperty_setValue(&obj->m_value, v); XVariant_delete_base((XClass*)v);
	}
	if (s_signalCount != 1) { TEST_FAIL("直写信号", "应收到1次"); goto cleanup; }
	if (!is_int(XObjectBindableProperty_value_const(&obj->m_value), 5)) { TEST_FAIL("对象属性读取", "应为5"); goto cleanup; }

	/* 绑定：依赖变化级联即发信号（对标 Qt Asynchronous 语义），槽内重读得新值 */
	{
		SumCtx* ctx = (SumCtx*)XMalloc_System(sizeof(SumCtx));
		if (!ctx) { TEST_FAIL("分配上下文", "返回NULL"); goto cleanup; }
		ctx->a = src; ctx->b = src;
		{
			XVariant* vs = make_int(2); XProperty_setValue(src, vs); XVariant_delete_base((XClass*)vs);
		}
		if (!XObjectBindableProperty_setBinding_eval(&obj->m_value, eval_sum, ctx, NULL))
		{ XFree_System(ctx); TEST_FAIL("设置绑定", "失败"); goto cleanup; }
		(void)XObjectBindableProperty_value_const(&obj->m_value);   /* 首次求值 m_value=4 */
		s_signalCount = 0;
		{
			XVariant* vs = make_int(3); XProperty_setValue(src, vs); XVariant_delete_base((XClass*)vs);
		}
		if (s_signalCount < 1) { TEST_FAIL("级联信号", "依赖变化应发 notify 信号"); goto cleanup; }
		if (!is_int(XObjectBindableProperty_value_const(&obj->m_value), 6)) { TEST_FAIL("绑定重算", "3+3!=6"); goto cleanup; }
		{
			XPropertyBinding* taken = XObjectBindableProperty_takeBinding(&obj->m_value);
			if (taken) XPropertyBinding_unref(taken);
		}
		XFree_System(ctx);
	}
	pass = true;
cleanup:
	if (src) XProperty_delete_base((XClass*)src);
	/* 对象参与信号槽：对标 Qt 用 deleteLater 经事件循环安全释放（连接随析构自动断开） */
	if (obj) XObject_deleteLater((XObject*)obj);
	if (receiver) XObject_deleteLater(receiver);
	XCoreApplication_processEvents(XEventLoop_AllEvents);   /* 泵一次事件队列，执行延迟删除 */
	if (pass) TEST_PASS("对象绑定属性 notify 信号");
	return pass;
}

/* ==================== 测试 9：ChangeHandler 返回 true 自动注销 ==================== */

static bool handler_once(void* user, const XVariant* newValue)
{
	(void)newValue;
	(*(int*)user)++;
	return true;   /* 处理一次后自动注销 */
}

static bool test_change_handler_once(void)
{
	TEST_INFO("===== ChangeHandler 自动注销 =====");
	bool pass = false;
	int fired = 0;
	XProperty* p = XProperty_create();
	XPropertyObserver* obs = NULL;
	if (!p) { TEST_FAIL("创建属性", "返回NULL"); goto cleanup; }
	obs = XPropertyObserver_create();
	if (!obs) { TEST_FAIL("创建观察者", "返回NULL"); goto cleanup; }
	XPropertyObserver_setHandler(obs, handler_once, &fired);
	XPropertyObserver_observe_property(obs, p);
	{
		XVariant* v = make_int(1); XProperty_setValue(p, v); XVariant_delete_base((XClass*)v);
		XVariant* v2 = make_int(2); XProperty_setValue(p, v2); XVariant_delete_base((XClass*)v2);
	}
	if (fired != 1) { TEST_FAIL("自动注销", "应只处理一次"); goto cleanup; }
	pass = true;
cleanup:
	if (obs) XPropertyObserver_delete(obs);   /* 已解挂，delete 安全 */
	if (p) XProperty_delete_base((XClass*)p);
	if (pass) TEST_PASS("ChangeHandler 自动注销");
	return pass;
}

/* ==================== 测试 10：错误对象 ==================== */

static bool test_error_object(void)
{
	TEST_INFO("===== 绑定错误对象 =====");
	bool pass = false;
	XPropertyBindingError err;
	XPropertyBindingError_init(&err);
	if (XPropertyBindingError_type(&err) != XPropertyBindingError_NoError) { TEST_FAIL("默认错误", "应为 NoError"); goto cleanup; }
	{
		XPropertyBindingError_init_ex_2(&err, XPropertyBindingError_InvalidBindingType, "bad type");
		if (XPropertyBindingError_type(&err) != XPropertyBindingError_InvalidBindingType) { TEST_FAIL("错误类型", "不符"); goto cleanup; }
		{
			const XString* desc = XPropertyBindingError_description_const(&err);
			if (!desc || !XString_equals_utf8(desc, "bad type", XChar_CaseInsensitive))
			{ TEST_FAIL("错误描述", "不符"); goto cleanup; }
		}
		XPropertyBindingError_clear(&err);
		if (XPropertyBindingError_type(&err) != XPropertyBindingError_NoError) { TEST_FAIL("clear", "应复位"); goto cleanup; }
	}
	/* 堆创建与拷贝 */
	{
		XPropertyBindingError* heap = XPropertyBindingError_create_ex_2(XPropertyBindingError_BindingLoop, "loop!");
		XPropertyBindingError* copy = heap ? XPropertyBindingError_create_copy(heap) : NULL;
		if (!heap || !copy) { if (heap) XPropertyBindingError_delete_base((XClass*)heap); TEST_FAIL("堆创建", "失败"); goto cleanup; }
		if (XPropertyBindingError_type(copy) != XPropertyBindingError_BindingLoop) { TEST_FAIL("拷贝错误", "类型不符"); goto cleanup; }
		XPropertyBindingError_delete_base((XClass*)copy);
		XPropertyBindingError_delete_base((XClass*)heap);
	}
	pass = true;
cleanup:
	XPropertyBindingError_deinit_base((XClass*)&err);
	if (pass) TEST_PASS("绑定错误对象");
	return pass;
}

/* ==================== 测试 11：可绑定门面 ==================== */

static bool eval_double(void* user, XVariant* out)
{
	XProperty* p = (XProperty*)user;
	int32_t v = XVariant_toInt32(XProperty_value_const(p)) * 2;
	XVariant_init(out, &v, sizeof(v), XVariantType_Int32);
	return true;
}

static bool test_bindable(void)
{
	TEST_INFO("===== 可绑定门面 =====");
	bool pass = false;
	XProperty *a = XProperty_create(), *c = XProperty_create();
	XBindable bindable;
	XUntypedBindable untyped;
	if (!a || !c) { TEST_FAIL("创建属性", "返回NULL"); goto cleanup; }
	{
		XVariant* va = make_int(21); XProperty_setValue(a, va); XVariant_delete_base((XClass*)va);
	}
	XBindable_init_property(&bindable, a);
	if (!XUntypedBindable_isValid(&bindable)) { TEST_FAIL("门面有效", "失败"); goto cleanup; }
	{
		XVariant* v = make_int(50); TEST_INFO("b1a:v");
		XBindable_setValue(&bindable, v); TEST_INFO("b1b:set");
		XVariant_delete_base((XClass*)v);
	}
	if (!is_int(XBindable_value_const(&bindable), 50)) { TEST_FAIL("门面写入", "应为50"); goto cleanup; }

	/* 经门面设置绑定 */
	{
		XUntypedBindable_init(&untyped);
		XBindable_init_property(&untyped, c);   /* 公开门面,禁止在测试中直接取内部接口 */
		(void)XUntypedBindable_setBinding_eval(&untyped, eval_double, a, NULL);
		if (!XUntypedBindable_hasBinding(&untyped)) { TEST_FAIL("门面绑定", "失败"); goto cleanup; }
		if (!is_int(XProperty_value_const(c), 100))
		{ TEST_FAIL("门面绑定求值", "21*2!=100"); goto cleanup; }
		{
			XPropertyBinding* taken = XUntypedBindable_takeBinding(&untyped);
			if (!taken || XUntypedBindable_hasBinding(&untyped)) { if (taken) XPropertyBinding_unref(taken); TEST_FAIL("门面takeBinding", "失败"); goto cleanup; }
			XPropertyBinding_unref(taken);
		}
	}
	pass = true;
cleanup:
	if (a) XProperty_delete_base((XClass*)a);
	if (c) XProperty_delete_base((XClass*)c);
	if (pass) TEST_PASS("可绑定门面");
	return pass;
}


#if XTHREAD_ON
/* ==================== 测试 12/13/14：多线程（对标 Qt 线程语义） ====================
 * Qt 语义：QProperty 本身非线程安全，跨线程读写需外部同步；绑定求值在
 * 执行读取的线程上完成，依赖捕获（thread_local 求值栈）按线程隔离。
 * 本库同构：求值栈位于 XThreadData::m_bindingEvalStack（每线程一份）。 */

/* 线程求值上下文：自己的属性与绑定 */
typedef struct ThCtx {
    XProperty* local;       /* 线程私有属性 */
    XProperty* mirror;      /* 绑定目标：mirror = local + 1 */
    XAtomic_int32_t done;   /* 完成计数（原子） */
    int32_t result;         /* 镜像求值结果 */
} ThCtx;

typedef struct CrossCtx {
    XProperty* shared;      /* 两线程共读的属性 */
    XAtomic_int32_t done;   /* 完成计数（原子） */
} CrossCtx;

/* mirror = local + 1 */
static bool th_eval_inc(void* user, XVariant* out)
{
    XProperty* p = (XProperty*)user;
    int32_t v;
    memset(out, 0, sizeof(XVariant));   /* 栈变体先清零 */
    XVariant_init(out, NULL, 0, XVariantType_NULL);
    if (!p) return false;
    v = XVariant_toInt32(XProperty_value_const(p)) + 1;
    XVariant_init(out, &v, sizeof(v), XVariantType_Int32);
    return true;
}

/* shared 恒等（依赖捕获路径） */
static bool th_eval_shared(void* user, XVariant* out)
{
    XProperty* p = (XProperty*)user;
    const XVariant* v;
    memset(out, 0, sizeof(XVariant));
    XVariant_init(out, NULL, 0, XVariantType_NULL);
    if (!p) return false;
    v = XProperty_value_const(p);       /* 登记依赖到本线程求值栈顶 */
    if (v && v->m_data)
        XVariant_init(out, v->m_data, v->m_dataSize, v->m_type);
    return true;
}

/* 线程 1 入口：local = 10；mirror = local + 1（线程内完整求值闭环） */
static void th_worker1_func(XThread* thread, XVarList* list)
{
    XVariant* v;
    XPropertyBinding* b;
    (void)thread;
    XVarList_start(list);
    ThCtx* ctx = *(ThCtx**)XVarList_argPtr(list);

    v = XVariant_create_int32(10);
    XProperty_setValue(ctx->local, v);
    XVariant_delete_base((XClass*)v);

    b = XProperty_setBinding_eval(ctx->mirror, th_eval_inc, ctx->local, NULL);
    if (!b) { XAtomic_fetch_add_int32(&ctx->done, 1, XAtomic_MemoryOrder_Release); return; }
    {
        const XVariant* r = XProperty_value_const(ctx->mirror);
        ctx->result = r ? XVariant_toInt32(r) : -1;
    }
    XProperty_takeBinding(ctx->mirror);
    XPropertyBinding_unref(b);
    XAtomic_fetch_add_int32(&ctx->done, 1, XAtomic_MemoryOrder_Release);
}

/* 线程 2 入口：工作线程求值读共享属性（依赖捕获走工作线程求值栈） */
static void th_worker2_func(XThread* thread, XVarList* list)
{
    XPropertyBinding* b;
    const XVariant* r;
    (void)thread;
    XVarList_start(list);
    CrossCtx* ctx = *(CrossCtx**)XVarList_argPtr(list);

    b = XProperty_setBinding_eval(ctx->shared, th_eval_shared, ctx->shared, NULL);
    if (!b) { XAtomic_fetch_add_int32(&ctx->done, 1, XAtomic_MemoryOrder_Release); return; }
    r = XProperty_value_const(ctx->shared);
    (void)r;
    XProperty_takeBinding(ctx->shared);
    XPropertyBinding_unref(b);
    XAtomic_fetch_add_int32(&ctx->done, 1, XAtomic_MemoryOrder_Release);
}

/* ==================== 测试 12：线程内绑定求值闭环 ==================== */

static bool test_thread_local_eval(void)
{
    TEST_INFO("===== 多线程:线程内绑定求值 =====");
    bool pass = false;
    ThCtx ctx;
    XThread* t1;
    memset(&ctx, 0, sizeof(ctx));
    ctx.local = XProperty_create();
    ctx.mirror = XProperty_create();
    XAtomic_init(ctx.done, 0);
    if (!ctx.local || !ctx.mirror) { TEST_FAIL("创建属性", "返回NULL"); goto cleanup; }

    ThCtx* pctx1 = &ctx;
    t1 = XThread_create_func(th_worker1_func,
        XVarList_Create(XVar(ThCtx*, pctx1)));
    if (!t1) { TEST_FAIL("创建线程", "失败"); goto cleanup; }
    XThread_start(t1);
    while (XAtomic_load_int32(&ctx.done, XAtomic_MemoryOrder_Acquire) < 1) {
        XCoreApplication_processEvents(XEventLoop_AllEvents);
    }
    XThread_wait(t1, 5000);
    XThread_deleteLater((XObject*)t1);
    XCoreApplication_processEvents(XEventLoop_AllEvents);

    if (ctx.result != 11) { TEST_FAIL("线程内求值", "10+1!=11"); goto cleanup; }
    pass = true;
cleanup:
    if (ctx.local) XProperty_delete_base((XClass*)ctx.local);
    if (ctx.mirror) XProperty_delete_base((XClass*)ctx.mirror);
    if (pass) TEST_PASS("多线程:线程内绑定求值");
    return pass;
}

/* ==================== 测试 13：依赖捕获按线程隔离 ==================== */

static bool test_thread_dep_isolation(void)
{
    TEST_INFO("===== 多线程:依赖捕获隔离 =====");
    bool pass = false;
    CrossCtx ctx;
    XPropertyBinding* mainBinding = NULL;
    const XVariant* r;
    XThread* t2;
    memset(&ctx, 0, sizeof(ctx));
    ctx.shared = XProperty_create();
    XAtomic_init(ctx.done, 0);
    if (!ctx.shared) { TEST_FAIL("创建属性", "返回NULL"); goto cleanup; }
    {
        XVariant* v = make_int(100);
        XProperty_setValue(ctx.shared, v);
        XVariant_delete_base((XClass*)v);
    }
    /* 主线程先做一次求值（占住主线程求值栈的捕获路径） */
    mainBinding = XPropertyBinding_create_eval(th_eval_shared, ctx.shared, NULL);
    if (!mainBinding) { TEST_FAIL("创建绑定", "失败"); goto cleanup; }
    r = XProperty_value_const(ctx.shared);
    (void)r;
    XPropertyBinding_unref(mainBinding);
    mainBinding = NULL;

    /* 工作线程再做一次求值:其依赖登记只进工作线程求值栈 */
    CrossCtx* pctx2 = &ctx;
    t2 = XThread_create_func(th_worker2_func,
        XVarList_Create(XVar(CrossCtx*, pctx2)));
    if (!t2) { TEST_FAIL("创建线程", "失败"); goto cleanup; }
    XThread_start(t2);
    while (XAtomic_load_int32(&ctx.done, XAtomic_MemoryOrder_Acquire) < 1) {
        XCoreApplication_processEvents(XEventLoop_AllEvents);
    }
    XThread_wait(t2, 5000);
    XThread_deleteLater((XObject*)t2);
    XCoreApplication_processEvents(XEventLoop_AllEvents);

    /* 关键验证:工作线程求值结束后,共享属性上的观察者链表已全部解挂
       (takeBinding+unref 释放了工作线程的依赖节点);主线程再写再读,
       无悬垂节点、值正确。若依赖捕获串扰到别的线程栈,此处会 UAF。 */
    {
        XVariant* v2 = make_int(200);
        XProperty_setValue(ctx.shared, v2);
        XVariant_delete_base((XClass*)v2);
    }
    if (!is_int(XProperty_value_const(ctx.shared), 200)) { TEST_FAIL("跨线程后读取", "应为200"); goto cleanup; }
    pass = true;
cleanup:
    if (mainBinding) XPropertyBinding_unref(mainBinding);
    if (ctx.shared) XProperty_delete_base((XClass*)ctx.shared);
    if (pass) TEST_PASS("多线程:依赖捕获隔离");
    return pass;
}

/* ==================== 测试 14：共享属性并发写（外部同步边界） ==================== */

#define TH_RW_THREADS 2
static XProperty* g_sharedProp = NULL;
static XAtomic_int32_t g_rwDone;

/* 各线程写各自的值到共享属性,再回读快照验证结构体完好 */
static void th_rw_func(XThread* thread, XVarList* list)
{
    const XVariant* v;
    (void)thread;
    XVarList_start(list);
    int32_t mine = *(int32_t*)XVarList_argPtr(list);
    {
        XVariant* nv = XVariant_create_int32(mine);
        XProperty_setValue(g_sharedProp, nv);
        XVariant_delete_base((XClass*)nv);
    }
    v = XProperty_value_const(g_sharedProp);
    if (v && v->m_data) {
        int32_t got = XVariant_toInt32(v);
        if (got != 1 && got != 2) {
            /* 读到撕裂/非法值:结构体竞态损坏 */
            XAtomic_store_int32(&g_rwDone, 0x7FFFFFFF, XAtomic_MemoryOrder_Release);
            return;
        }
    }
    XAtomic_fetch_add_int32(&g_rwDone, 1, XAtomic_MemoryOrder_Release);
}

static bool test_thread_shared_rw(void)
{
    TEST_INFO("===== 多线程:共享属性并发写 =====");
    bool pass = false;
    XThread* threads[TH_RW_THREADS];
    int32_t vals[TH_RW_THREADS];
    int i;
    const XVariant* final;

    g_sharedProp = XProperty_create();
    XAtomic_init(g_rwDone, 0);
    if (!g_sharedProp) { TEST_FAIL("创建属性", "返回NULL"); goto cleanup; }
    vals[0] = 1; vals[1] = 2;

    int32_t pvals[TH_RW_THREADS];
    pvals[0] = vals[0]; pvals[1] = vals[1];
    for (i = 0; i < TH_RW_THREADS; i++) {
        threads[i] = XThread_create_func(th_rw_func,
            XVarList_Create(XVar(int32_t, pvals[i])));
        if (!threads[i]) { TEST_FAIL("创建线程", "失败"); goto cleanup; }
    }
    for (i = 0; i < TH_RW_THREADS; i++) XThread_start(threads[i]);
    while (XAtomic_load_int32(&g_rwDone, XAtomic_MemoryOrder_Acquire) < TH_RW_THREADS) {
        XCoreApplication_processEvents(XEventLoop_AllEvents);
    }
    for (i = 0; i < TH_RW_THREADS; i++) {
        XThread_wait(threads[i], 5000);
        XThread_deleteLater((XObject*)threads[i]);
    }
    XCoreApplication_processEvents(XEventLoop_AllEvents);

    /* 最终值必是两写者之一,且内部状态完好(可读、类型正确) */
    final = XProperty_value_const(g_sharedProp);
    if (!is_int(final, 1) && !is_int(final, 2)) { TEST_FAIL("并发写终值", "非法值"); goto cleanup; }
    pass = true;
cleanup:
    if (g_sharedProp) XProperty_delete_base((XClass*)g_sharedProp);
    g_sharedProp = NULL;
    if (pass) TEST_PASS("多线程:共享属性并发写");
    return pass;
}
#endif /* XTHREAD_ON */

/* ==================== 综合入口与菜单注册 ==================== */

void XPropertyTestAll(void)
{
	int passed = 0, total = 0;
	TEST_INFO("\n========== XProperty 属性绑定测试 ==========");
	total++; if (test_basic()) passed++;
	total++; if (test_binding_eval()) passed++;
	total++; if (test_cascade_notify()) passed++;
	total++; if (test_dynamic_dependency()) passed++;
	total++; if (test_binding_loop()) passed++;
	total++; if (test_binding_break()) passed++;
	total++; if (test_alias()) passed++;
	total++; if (test_object_property()) passed++;
	total++; if (test_change_handler_once()) passed++;
	total++; if (test_error_object()) passed++;
	total++; if (test_bindable()) passed++;
#if XTHREAD_ON
	total++; if (test_thread_local_eval()) passed++;
	total++; if (test_thread_dep_isolation()) passed++;
	total++; if (test_thread_shared_rw()) passed++;
#endif /* XTHREAD_ON */
	TEST_INFO("========================================");
	TEST_INFO("    测试结果: %d/%d 通过", passed, total);
	TEST_INFO("========================================\n");
}

static void XPropertyTestAll_wrapper(XVariant* data) { (void)data; XPropertyTestAll(); }
static void test_basic_wrapper(XVariant* data) { (void)data; test_basic(); }
static void test_binding_eval_wrapper(XVariant* data) { (void)data; test_binding_eval(); }
static void test_cascade_notify_wrapper(XVariant* data) { (void)data; test_cascade_notify(); }
static void test_dynamic_dependency_wrapper(XVariant* data) { (void)data; test_dynamic_dependency(); }
static void test_binding_loop_wrapper(XVariant* data) { (void)data; test_binding_loop(); }
static void test_binding_break_wrapper(XVariant* data) { (void)data; test_binding_break(); }
static void test_alias_wrapper(XVariant* data) { (void)data; test_alias(); }
static void test_object_property_wrapper(XVariant* data) { (void)data; test_object_property(); }
static void test_change_handler_once_wrapper(XVariant* data) { (void)data; test_change_handler_once(); }
static void test_error_object_wrapper(XVariant* data) { (void)data; test_error_object(); }
static void test_bindable_wrapper(XVariant* data) { (void)data; test_bindable(); }
#if XTHREAD_ON
static void test_thread_local_eval_wrapper(XVariant* data) { (void)data; test_thread_local_eval(); }
static void test_thread_dep_isolation_wrapper(XVariant* data) { (void)data; test_thread_dep_isolation(); }
static void test_thread_shared_rw_wrapper(XVariant* data) { (void)data; test_thread_shared_rw(); }
#endif /* XTHREAD_ON */

void XTestMenu_XPropertyTest(XTestMenu* root)
{
	XTestMenu* menu = XTestMenu_create("XProperty(属性绑定)");
	XTestMenu_addMenu(root, menu);
	{
		XAction* action = XTestMenu_addAction(menu, "综合测试(全部)");
		XTestMenu_setActionFunction(action, XPropertyTestAll_wrapper);

		XAction* a1 = XTestMenu_addAction(menu, "基础读写");
		XTestMenu_setActionFunction(a1, test_basic_wrapper);
		XAction* a2 = XTestMenu_addAction(menu, "绑定求值与依赖捕获");
		XTestMenu_setActionFunction(a2, test_binding_eval_wrapper);
		XAction* a3 = XTestMenu_addAction(menu, "级联通知");
		XTestMenu_setActionFunction(a3, test_cascade_notify_wrapper);
		XAction* a4 = XTestMenu_addAction(menu, "动态依赖重捕获");
		XTestMenu_setActionFunction(a4, test_dynamic_dependency_wrapper);
		XAction* a5 = XTestMenu_addAction(menu, "绑定循环检测");
		XTestMenu_setActionFunction(a5, test_binding_loop_wrapper);
		XAction* a6 = XTestMenu_addAction(menu, "赋值断开与takeBinding");
		XTestMenu_setActionFunction(a6, test_binding_break_wrapper);
		XAction* a7 = XTestMenu_addAction(menu, "属性别名");
		XTestMenu_setActionFunction(a7, test_alias_wrapper);
		XAction* a8 = XTestMenu_addAction(menu, "对象绑定属性notify信号");
		XTestMenu_setActionFunction(a8, test_object_property_wrapper);
		XAction* a9 = XTestMenu_addAction(menu, "ChangeHandler自动注销");
		XTestMenu_setActionFunction(a9, test_change_handler_once_wrapper);
		XAction* a10 = XTestMenu_addAction(menu, "绑定错误对象");
		XTestMenu_setActionFunction(a10, test_error_object_wrapper);
		XAction* a11 = XTestMenu_addAction(menu, "可绑定门面");
		XTestMenu_setActionFunction(a11, test_bindable_wrapper);
#if XTHREAD_ON
		XAction* a12 = XTestMenu_addAction(menu, "多线程:线程内绑定求值");
		XTestMenu_setActionFunction(a12, test_thread_local_eval_wrapper);
		XAction* a13 = XTestMenu_addAction(menu, "多线程:依赖捕获隔离");
		XTestMenu_setActionFunction(a13, test_thread_dep_isolation_wrapper);
		XAction* a14 = XTestMenu_addAction(menu, "多线程:共享属性并发写");
		XTestMenu_setActionFunction(a14, test_thread_shared_rw_wrapper);
#endif /* XTHREAD_ON */
	}
}

#endif /* DEMOTEST && XPROPERTY_ON */
