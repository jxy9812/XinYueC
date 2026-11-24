/* -*- coding: utf-8 -*-
 * 全局 store：SSE 事件落库 + 订阅主题收敛。
 *
 * [单一数据源] 后端把全部推送解一次存 AppModel，所有浏览器共享
 * （model.py AppModel 文档）；前端同理：store 持每类最新帧，页面组件
 * 挂载时先读快照、再订阅增量，切页不丢数据。
 *
 * [订阅收敛] 后端按「所有 SSE 连接的主题并集」向上游订阅
 * （hub.desired_topics/_reconcile，hub.py:419-455），本连接的意图用
 * POST /api/topics 带 connId 上报（api.py:529-557；一个浏览器多标签页
 * 各停各页互不覆盖）。全局常驻主题温湿度/健康由后端 GLOBAL_TOPICS
 * 无条件叠加（hub.py:91-94），前端不必列。
 *
 * [重连重发] SSE 每次重连成功都会重建服务端订阅者（connId 变化）⇒
 * 必须**重发**当前主题意图（参考客户端 2026-11-07「重连后重发订阅帧」
 * 同款口径），本模块在 sse status=open 时自动做。
 */
(function () {
  "use strict";
  var HSF = (window.HSF = window.HSF || {});

  /* [漏登记 = 整页永久静默] applyEvent 按这张白名单过滤 ⇒ 少一个名字，
   * 该类事件**全部被丢弃**、页面永远显示「未收到画面」，而后端明明在发
   * （与后端 hub.TOPIC_KIND 同款坑，两边都得登记）。
   * camera = 相机画面的**元信息**（帧本体不走 SSE，见 /api/camera/frame）。*/
  var KINDS = ["snapshot", "vfd", "temphum", "analog", "flowmeter",
               "turbidity", "pump", "digital", "valve", "health",
               "mode", "alarm", "link", "jog", "camera"];

  var store = {
    /** 每类最新数据（kinds 同上）。 */
    data: {},
    /** 当前 SSE connId（topics 上报用）。 */
    connId: "",
    /** 上游链路：{ready, detail, user…}（hub._link_json 形状）。 */
    link: { ready: false, detail: "" },
    /** 本连接当前主题意图（内部）。 */
    _topics: [],
    /** 页面声明的主题（router 切页时 updatePageTopics）。 */
    _pageTopics: [],
    /** 页面之外的临时主题（悬浮相机卡打开期间的相机流）。 */
    _extraTopics: [],
    _bus: HSF.makeBus(),
    _sse: null,

    on: function (kind, fn) { return this._bus.on(kind, fn); },

    /** 当前页面需要的主题（router 切页调；MENU 页给 []）。 */
    updatePageTopics: function (topics) {
      this._pageTopics = (topics || []).slice();
      this._pushTopics();
    },

    /**
     * 追加一个临时主题（**不随页面切换失效**）。用于悬浮相机卡：
     * 开卡时加相机主题、关卡时移除 ⇒ 关闭后上游停止推大流量帧。
     * 幂等（重复 add 不重复上报）。
     */
    addExtraTopic: function (topic) {
      topic = Number(topic);
      if (!topic || this._extraTopics.indexOf(topic) >= 0) return;
      this._extraTopics.push(topic);
      this._pushTopics();
    },

    /** 移除临时主题（关卡时调；没有也无害）。 */
    removeExtraTopic: function (topic) {
      topic = Number(topic);
      var i = this._extraTopics.indexOf(topic);
      if (i < 0) return;
      this._extraTopics.splice(i, 1);
      this._pushTopics();
    },

    /** 上报主题意图（SSE open 与切页两路都走这里）。 */
    _pushTopics: function () {
      /* [未登录不上报 2026-11-27 实锅：页面盖在登录页上] `trendhist.js`
       * 在**模块加载期**就 addExtraTopic 三个常驻趋势主题 ⇒ 此时 boot 还没
       * 拿到 authtoken ⇒ 这 3 条 POST /api/topics 必吃 401 ⇒ api.js 广播
       * unauthorized ⇒ app 走「会话过期」分支：清掉 boot 即将/刚设置的
       * token + 把登录页翻上来；随后 authtoken 直登虽然进了壳，但
       * `_enterShell` 不隐藏登录页 ⇒ 页面停在登录页（实测 webui_check 的
       * 「自动循环实例化」段 11 连红与 run2 截图「登录会话已过期」）。
       * ⇒ **没有 token 时直接跳过**；登录后 SSE open 会再走这里补发。 */
      if (!HSF.api.getToken()) return;
      var topics = this._pageTopics.concat(this._extraTopics);
      /* 去重（页面本身可能已含相机主题） */
      topics = topics.filter(function (t, i) {
        return topics.indexOf(t) === i;
      });
      var same = topics.length === this._topics.length &&
        topics.every(function (t, i) { return t === store._topics[i]; });
      if (same && this._topicsSentForConn === this.connId) return;   /* 同值早退 */
      this._topics = topics;
      this._topicsSentForConn = this.connId;
      HSF.api.setTopics(this.connId || null, topics).then(function (doc) {
        if (!doc || doc.ok !== true) {
          console.warn("[store] 订阅主题上报失败：", doc && doc.err);
        }
      });
    },

    /** 强制重发主题意图（**自愈入口**，跳过"同值早退"）。
     *
     * [2026-11-23 实锅：相机悬浮窗画面冻住] 页面 SSE 还活着（其他数据在变），
     * 但上游侧对该主题的订阅可能已丢（web 进程/上游链路重启窗口、服务端
     * 会话复位都会清订阅表）⇒ store 收不到新帧 ⇒ 悬浮窗画面停在最后一帧，
     * 而"同值早退"让 addExtraTopic/切页都不会再上报 ⇒ 永不自愈。
     * 调用方（相机取流看门狗）按"seq 连续 5s 不动"判定后调本方法。
     */
    repushTopics: function () {
      this._topicsSentForConn = null;     /* 让 _pushTopics 不再同值早退 */
      this._pushTopics();
    },

    /** 装配 SSE（登录成功后调一次；内部自动重连）。 */
    connect: function () {
      if (this._sse) this._sse.stop();
      var self = this;
      this._sse = new HSF.SseClient(
        function (name, data) { self.applyEvent(name, data); },
        function (state, detail) {
          if (state === "open") {
            self.connId = detail || "";
            self._pushTopics();               /* 重连后重发订阅 */
          }
          self._bus.emit("sse", { state: state, detail: detail });
        });
      this._sse.start(HSF.api.getToken());
    },

    disconnect: function () {
      if (this._sse) this._sse.stop();
      this._sse = null;
      this.connId = "";
      this._topics = [];
      this._topicsSentForConn = null;
    },

    /** SSE 事件落库并广播（app.js 启动时也会用它灌 /api/state 快照）。 */
    applyEvent: function (name, data) {
      if (KINDS.indexOf(name) < 0) return;
      if (name === "snapshot") {
        /* 首帧全量：逐类落库（vfd/temphum/…/mode），不发 snapshot 事件本身。 */
        Object.keys(data || {}).forEach(function (k) {
          if (KINDS.indexOf(k) >= 0 && k !== "snapshot") {
            store.data[k] = data[k];
            store._bus.emit(k, data[k]);
          }
        });
        return;
      }
      if (name === "link") {
        this.data.link = data;
        this.link = data || { ready: false };
      } else {
        this.data[name] = data;
      }
      this._bus.emit(name, data);
    },

    /** 取某类当前值（无则 def）。 */
    get: function (kind, def) {
      var v = this.data[kind];
      return v === undefined ? def : v;
    },
  };

  HSF.store = store;
})();
