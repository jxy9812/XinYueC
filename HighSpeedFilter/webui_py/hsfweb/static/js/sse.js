/* -*- coding: utf-8 -*-
 * SSE 订阅（/api/stream）与自动重连。
 *
 * [为什么不用 EventSource] EventSource 发不了 Authorization 头（浏览器
 * 不允许自定义头）；后端虽接受 hsf_token Cookie 兜底（httpd.py:105-110），
 * 但显式 Bearer 与 REST 同一条鉴权路更直——故用 fetch + ReadableStream
 * 手工解帧（event:/data:/id: 行，帧格式见 httpd.py sse_event）。
 *
 * [对齐后端事件名] hub.TOPIC_KIND（hub.py:71-84）+ snapshot/link/mode/
 * health/alarm；首帧必是 snapshot（register_subscriber 时必投，
 * smoke_backend.py §6 断言）。
 *
 * [重连口径] 指数退避 1s→5s 封顶；连上后把 connId（响应头
 * X-Hsf-Conn-Id，api.py:525）记到 store，topics 收敛由 store 负责
 * （重连后必须重发订阅——参考客户端 2026-11-07 口径）。
 */
(function () {
  "use strict";
  var HSF = (window.HSF = window.HSF || {});

  var RETRY_BASE_MS = 1000;
  var RETRY_MAX_MS = 5000;

  function SseClient(onEvent, onStatus) {
    this.onEvent = onEvent;       /* (name, data) */
    this.onStatus = onStatus;     /* ("connecting"|"open"|"closed", detail) */
    this._token = "";
    this._closed = true;          /* true = 用户 stop() 过，不再重连 */
    this._controller = null;
    this._retryMs = RETRY_BASE_MS;
    this._retryTimer = null;
    this._runId = 0;
  }

  SseClient.prototype.start = function (token) {
    this._token = token || "";
    this._closed = false;
    this._connect();
  };

  SseClient.prototype.stop = function () {
    this._closed = true;
    if (this._retryTimer) { clearTimeout(this._retryTimer); this._retryTimer = null; }
    if (this._controller) { try { this._controller.abort(); } catch (e) { /* 忽略 */ } }
    this._controller = null;
  };

  SseClient.prototype._connect = async function () {
    if (this._closed) return;
    var runId = ++this._runId;
    var self = this;
    if (this.onStatus) this.onStatus("connecting", "");
    this._controller = new AbortController();
    var headers = { "Accept": "text/event-stream" };
    if (this._token) headers["Authorization"] = "Bearer " + this._token;
    var resp;
    try {
      resp = await fetch("/api/stream", {
        headers: headers,
        signal: this._controller.signal,
        cache: "no-store",
      });
    } catch (err) {
      if (runId !== this._runId || this._closed) return;
      if (this.onStatus) this.onStatus("closed", "连接失败");
      this._scheduleRetry();
      return;
    }
    if (!resp.ok || !resp.body) {
      if (runId !== this._runId || this._closed) return;
      if (this.onStatus) this.onStatus("closed", "HTTP " + resp.status);
      this._scheduleRetry();
      return;
    }
    var connId = resp.headers.get("X-Hsf-Conn-Id") || "";
    if (this.onStatus) this.onStatus("open", connId);
    this._retryMs = RETRY_BASE_MS;              /* 连上即重置退避 */

    /* ---- 手工解 SSE 帧 ---- */
    var reader = resp.body.getReader();
    var decoder = new TextDecoder("utf-8");
    var buf = "";
    var evName = "";
    var dataLines = [];
    var self2 = this;
    try {
      while (true) {
        var chunk = await reader.read();
        if (chunk.done) break;
        if (runId !== this._runId) return;
        buf += decoder.decode(chunk.value, { stream: true });
        var idx;
        while ((idx = buf.indexOf("\n")) >= 0) {
          var line = buf.slice(0, idx).replace(/\r$/, "");
          buf = buf.slice(idx + 1);
          if (line === "") {                     /* 空行 = 帧结束 */
            if (dataLines.length) {
              var text = dataLines.join("\n");
              var data = null;
              try { data = JSON.parse(text); } catch (e) { data = null; }
              if (self2.onEvent && evName) self2.onEvent(evName, data, text);
            }
            evName = "";
            dataLines = [];
          } else if (line.indexOf(":") === 0) {
            /* 心跳注释行（httpd.py:515），忽略 */
          } else if (line.indexOf("event: ") === 0) {
            evName = line.slice(7).trim();
          } else if (line.indexOf("data: ") === 0) {
            dataLines.push(line.slice(6));
          } else if (line.indexOf("id: ") === 0) {
            /* 后端带 id 但不消费 Last-Event-ID（首帧 snapshot 兜底） */
          }
        }
      }
    } catch (err) {
      /* abort / 网络断：走下面的统一重连 */
    }
    if (runId !== this._runId || this._closed) return;
    if (this.onStatus) this.onStatus("closed", "连接断开");
    this._scheduleRetry();
  };

  SseClient.prototype._scheduleRetry = function () {
    if (this._closed) return;
    var self = this;
    var wait = this._retryMs;
    this._retryMs = Math.min(RETRY_MAX_MS, this._retryMs * 2);
    if (this.onStatus) this.onStatus("retry", wait);
    this._retryTimer = setTimeout(function () {
      self._retryTimer = null;
      self._connect();
    }, wait);
  };

  HSF.SseClient = SseClient;
})();
