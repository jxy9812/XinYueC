/* -*- coding: utf-8 -*-
 * 页 17 相机：显示服务端采集的网络相机画面。
 *
 * [页号 17] 16 是**保留槽位**（桌面客户端的「Web 设置」，网页端不实现）⇒
 * 两端页号逐号对齐（同一份操作手册的同一个页号，指的是同一页）。
 *
 * [数据来源：服务端，**浏览器不直连相机**] 用户口径（2026-11-19）：网页端
 * 也要从服务端取数据 —— 外网访问场景里浏览器在公网、相机在内网，只有服务端
 * 两边都够得到。画面经 XLink 分片到本机 webui，本页按模式取：
 *   · mode="jpeg"（旧路兜底）：<img src="/api/camera/frame?seq=N"> 轮询换图；
 *   · mode="es"（2026-11-19 架构转向，**缺省**）：服务端直传 H.264/H.265
 *     编码流（NAL），本页用 **WebCodecs VideoDecoder** 在浏览器里解码、
 *     canvas 渲染。增量数据走 `/api/camera/es?cursor=N`（二进制 NAL 流，
 *     画面本体照旧不过 SSE —— 那是文本通道，base64 +33%）。
 *
 * [鉴权靠 Cookie] fetch/<img> 自动带登录时下发的 hsf_token Cookie。
 *
 * [只在打开本页时才有画面] 本页声明 topics=[相机主题] ⇒ 切到本页才订阅
 * ⇒ 服务端按订阅门控推流（**没人订阅时一帧都不推**，用户 2026-11-19 口径）。
 *
 * [H.265 的现实] H.264 全浏览器可软解；H.265 只有带硬件解码的
 * Chrome/Edge/Safari 支持 ⇒ 进 es+h265 模式先 isConfigSupported 探测，
 * 不支持就明确提示（不要黑屏装死）。
 */
(function () {
  "use strict";
  var HSF = (window.HSF = window.HSF || {});

  var FRAME_URL = "/api/camera/frame";
  var ES_URL = "/api/camera/es";
  var POLL_MS = 1500;          /* jpeg 兜底轮询（毫秒） */
  var ES_FETCH_MS = 120;       /* ES 增量轮询（毫秒；20fps ≈ 50ms/帧） */
  var NO_FRAME_TIP = "等待画面（相机未启用 / 离线 / 正在连接）";

  /* [解码器探测顺序] H.264：Annex-B（无 description）+ 常用 profile 组合；
   * H.265：hev1 主档。探测不到 ⇒ 降级提示。 */
  var H264_CODECS = ["avc1.640029", "avc1.42E01E"];
  var H265_CODECS = ["hev1.1.6.L93.B0", "hvc1.1.6.L93.B0"];

  var activeUnmount = null;

  HSF.router.register(17, {
    title: "相机",
    topics: function () {
      var t = (HSF.registry && HSF.registry.pageTopics &&
               HSF.registry.pageTopics["17"]) || [];
      return t.length ? t : [0x0001000C];
    },
    mount: function (host, ctx) { activeUnmount = mountPage(host, false); },
    unmount: function () {
      if (activeUnmount) { activeUnmount(); activeUnmount = null; }
    },
  });

  /* ---- NAL 工具（与服务端 camera.py 同规则） ---- */
  function nalType(nal, h265) {
    var i = 0;
    while (i < nal.length - 1 && nal[i] === 0) i++;
    if (nal.length - i < 2) return -1;
    return h265 ? ((nal[i + 1] >> 1) & 0x3f) : (nal[i + 1] & 0x1f);
  }
  /* 去起始码（00 00 01 / 00 00 00 01），返回从 NAL header 开始的 body。 */
  function stripStartCode(nal) {
    var i = 0;
    while (i < nal.length - 1 && nal[i] === 0) i++;
    return nal.subarray(i + 1);
  }
  function isParam(t, h265) {
    return h265 ? (t === 32 || t === 33 || t === 34) : (t === 7 || t === 8);
  }
  function isIdr(t, h265) {
    return h265 ? (t >= 16 && t <= 21) : (t === 5);
  }

  function mountPage(host, compact) {
    var store = HSF.store;
    var unsubs = [];
    var timers = [];

    /* ---- DOM：img（jpeg 模式）与 canvas（es 模式）并存，按模式显隐 ---- */
    var img = HSF.el("img", { class: "camera-img", alt: "相机画面" });
    var canvas = HSF.el("canvas", { class: "camera-img" });
    canvas.style.display = "none";
    var placeholder = HSF.el("div", { class: "camera-placeholder" },
                             [HSF.el("span", { text: NO_FRAME_TIP })]);
    var statusLine = HSF.el("div", { class: "camera-status" });
    var metaLine = HSF.el("div", { class: "camera-meta" });
    /* [全屏 2026-11-19 用户口径「类似网站视频的播放界面」] 画面区整块
     * requestFullscreen（浏览器原生：F11 语义、Esc 退出），stage 元素
     * 全屏后由 CSS 铺满（theme.css 的 .camera-stage:fullscreen）。 */
    var fsBtn = HSF.el("button", { class: "btn camera-fs-btn",
                                   text: "全屏" });
    fsBtn.addEventListener("click", function () {
      var stage = canvas.parentNode;
      if (document.fullscreenElement) {
        document.exitFullscreen();
      } else if (stage.requestFullscreen) {
        stage.requestFullscreen();
      }
    });
    /* [双击全屏 2026-11-19 用户口径「双击可以全屏，再双击退出全屏」]
     * 与桌面端同款习惯操作：双击画面区进入/退出浏览器全屏。 */
    var stageEl = HSF.el("div", { class: "camera-stage" },
                         [img, canvas, placeholder]);
    stageEl.addEventListener("dblclick", function () {
      if (document.fullscreenElement) {
        document.exitFullscreen();
      } else if (stageEl.requestFullscreen) {
        stageEl.requestFullscreen();
      }
    });

    /* [compact 2026-11-22] 标题栏相机悬浮卡复用同一套取流/解码逻辑：
     * compact=true 只建画面区（无页标题/状态行/全屏钮），由悬浮卡自己
     * 撑满尺寸；全屏钮不影响取流，compact 下不挂进 DOM 即可。 */
    var pageKids = compact
      ? [stageEl]
      : [
        HSF.el("div", { class: "page-title", text: "相机" }),
        HSF.el("div", { class: "camera-topbar" }, [statusLine, fsBtn]),
        stageEl,
        metaLine,
      ];
    var page = HSF.el("div", {
      class: "page camera-page" + (compact ? " camera-compact" : ""),
    }, pageKids);
    host.appendChild(page);

    var mode = "jpeg";
    var codec = "h264";
    var lastSeq = -1;

    /* ---- ES 播放器状态 ---- */
    var es = null;              /* { decoder, params:[], cursor, busy, supported } */

    function camDoc() {
      return store.get("camera") || {};
    }
    function setText(node, text) {
      text = String(text == null ? "" : text);
      if (node.textContent !== text) node.textContent = text;
    }
    function fmtTime(tsMs) {
      if (!tsMs) return "--";
      var d = new Date(tsMs);
      function p(n) { return (n < 10 ? "0" : "") + n; }
      return p(d.getHours()) + ":" + p(d.getMinutes()) + ":" + p(d.getSeconds());
    }

    /* ================= jpeg 模式（旧路，原逻辑不动） ================= */
    function refreshImage() {
      var doc = camDoc();
      var seq = doc.seq || 0;
      if (!doc.hasFrame) {
        img.removeAttribute("src");
        img.style.display = "none";
        placeholder.style.display = "";
        setText(placeholder.firstChild, NO_FRAME_TIP);
        setText(statusLine, "未收到画面");
        setText(metaLine, "");
        lastSeq = -1;
        return;
      }
      if (seq === lastSeq) return;
      lastSeq = seq;
      /* [必须显式 block] 清空内联 display 会落回样式表的 display:none。 */
      img.style.display = "block";
      canvas.style.display = "none";
      placeholder.style.display = "none";
      img.onerror = function () {
        img.style.display = "none";
        placeholder.style.display = "";
        setText(placeholder.firstChild, "画面取不到（会话过期或服务端未连接）");
      };
      img.src = FRAME_URL + "?seq=" + seq + "&t=" + Date.now();
    }
    function renderMetaJpeg(doc) {
      var parts = [];
      if (doc.width && doc.height) parts.push(doc.width + "×" + doc.height);
      parts.push("帧号 " + (doc.seq || 0));
      if (doc.bytes) parts.push(Math.round(doc.bytes / 1024) + " KB");
      parts.push("更新 " + fmtTime(doc.tsMs));
      if (doc.dropped) parts.push("丢帧 " + doc.dropped);
      setText(metaLine, parts.join(" · "));
      setText(statusLine, doc.hasFrame ? "画面接收中（JPEG 转发）" : "未收到画面");
    }

    /* ================= es 模式（直传 + WebCodecs） =================
     * [2026-11-19 深挖后的正确用法——网上资料与本地实验双确认]
     * Chromium 的 WebCodecs H.264：**`avc1.*` + 无 description（Annex B）
     * = configure 成功但 decode 静默无输出/DataError 的已知坑**（社区与
     * Chromium issue 一致结论："If you're using AVC formatted H.264 you
     * must fill out the description field"）⇒ 本播放器用 **AVCC
     * description 模式**：
     *   · desc = 从流内 SPS/PPS 现场构造的 AVCDecoderConfigurationRecord
     *     （ISO 14496-15：version/profile/compat/level 取 SPS **真值**——
     *     codec 串里的 level 与 SPS 不一致会报 "Failed to parse avcC"，
     *     实测 avc1.640029 配 level 3.0 流直接 parse 失败）；
     *   · chunk = AVCC 格式（**去起始码**，[4 字节长度][NAL body]）；
     *   · key chunk = SPS+PPS+IDR 拼（首帧自举），后续 delta = 单 slice。
     * H.265（hevC）构造复杂且现场未用 ⇒ 仍走探测+提示路线。 */

    function pickCodec(list) {
      if (!window.VideoDecoder) return Promise.resolve(null);
      var i = 0;
      function next() {
        if (i >= list.length) return Promise.resolve(null);
        var c = list[i++];
        return VideoDecoder.isConfigSupported({ codec: c })
          .then(function (r) { return r && r.supported ? c : next(); })
          .catch(function () { return next(); });
      }
      return next();
    }

    var camDbg = (window.__camDbg = []);
    function dbg(tag) {
      if (camDbg.length > 240) camDbg.splice(0, 120);
      camDbg.push(tag + "@" + Date.now() % 100000);
    }

    function esStart(h265) {
      es = { decoder: null, cursor: 0, busy: false,
             supported: false, frames: 0, dropped: 0, waitKey: true,
             sps: null, pps: null, vps: null, configured: false,
             descErr: "", h265: h265, miss404: 0, lastTry: 0 };
      window.__camEs = es;          /* F12 排查口（页面卸载时摘除） */
      /* [H.265 网页端支持 2026-11-19] Edge/Chrome 107+ 支持 HEVC 解码
       * 但**仅硬件解码**（GPU 要有 HEVC 解码块；Edge 另需 HEVC Video
       * Extensions）⇒ 运行时 isConfigSupported 探测（hvc1/hev1）。
       * 本机 Intel UHD 770 实测硬件路径 true（prefer-software=false）。 */
      pickCodec(h265 ? H265_CODECS : H264_CODECS).then(function (codecStr) {
        if (!es) return;
        if (!codecStr) {
          es.supported = false;
          placeholder.style.display = "";
          canvas.style.display = "none";
          setText(placeholder.firstChild,
                  h265 ? "当前浏览器不支持 H.265 解码（需 GPU 硬解"
                       + "，Edge 另需 HEVC Video Extensions）：请把相机"
                       + "子码流切回 H.264，或用桌面客户端查看"
                       : "当前浏览器不支持编码流解码（WebCodecs 缺失）");
          setText(statusLine, "无法解码");
          return;
        }
        es.supported = true;
        es.codecStr = codecStr;
        setText(statusLine, "等相机流参数集（"
                + (h265 ? "VPS/SPS/PPS" : "SPS/PPS") + "）…");
        /* configure 在 esFeed 收齐参数集时做（desc 需要参数集真值）。 */
      });
    }

    function draw(vf) {
      canvas.style.display = "block";
      placeholder.style.display = "none";
      img.style.display = "none";
      /* 保持比例铺满（canvas CSS 尺寸由布局定，内部分辨率取视频原始尺寸） */
      if (canvas.width !== vf.displayWidth ||
          canvas.height !== vf.displayHeight) {
        canvas.width = vf.displayWidth;
        canvas.height = vf.displayHeight;
      }
      canvas.getContext("2d").drawImage(vf, 0, 0);
    }

    function esReset() {
      if (es && es.decoder) {
        try { es.decoder.close(); } catch (e) { /* ignore */ }
        es.decoder = null;
        es.configured = false;
      }
      /* [SPS/PPS 缓存**不清** 2026-11-19 实锅] 真相机（TP-LINK）子码流的
       * 参数集**只在会话建立时发一次**（单会话直传实验：15 秒只见到
       * 首个 SPS/PPS，之后 GOP 边界不重发）⇒ esReset 清了它们 ⇒ 流里
       * 再也没有新参数集 ⇒ **永久黑屏**。保留缓存，decoder 重建时用它
       * 自举（IDR 到来即恢复）。 */
      if (es) { es.frames = 0; es.dropped = 0; }   /* sps/pps/vps 保留 */
      esNeedKey();
    }
    function esNeedKey() { if (es) es.waitKey = true; }

    /* AVCC pack：[u32 长度][NAL body（无起始码）]。 */
    function avccPack(bodies) {
      var total = 0;
      bodies.forEach(function (b) { total += 4 + b.length; });
      var out = new Uint8Array(total);
      var off = 0;
      bodies.forEach(function (b) {
        var n = b.length;
        out[off] = (n >>> 24) & 255;
        out[off + 1] = (n >>> 16) & 255;
        out[off + 2] = (n >>> 8) & 255;
        out[off + 3] = n & 255;
        out.set(b, off + 4);
        off += 4 + n;
      });
      return out;
    }

    /* AVCDecoderConfigurationRecord（ISO 14496-15 §5.3.3.1）。
     * [三个易错点 2026-11-19 实验矩阵实锤]
     * ① numOfPictureParameterSets 是**整字节**（0x01），不是 SPS 计数那种
     *    带 111 前缀的 0xE1（0xE1=225 会让解析器读越界——纯参数集记录
     *    Chromium 恰好容忍，High profile 一追加扩展字节就爆）。
     * ② **High profile（0x64/0x6e/0x7a…）必须在 PPS 后追加 4 字节**：
     *    chroma_format_idc(1=4:2:0)、bit_depth_luma_minus8(0)、
     *    bit_depth_chroma_minus8(0)、numOfSequenceParameterSetsExt(0)
     *    ——缺 extCount 那字节（39 字节记录）照样 "Failed to parse avcC"。
     * ③ codec 串 hex **每字节补零**（SPS[2]=0x00 拼成 7 位=非法 codec）。 */
    function buildDescAndCodec(sps, pps) {
      var isHigh = (sps[1] === 0x64 || sps[1] === 0x6e
                    || sps[1] === 0x7a || sps[1] === 0xf4
                    || sps[1] === 0x2c || sps[1] === 0x53);
      var desc = new Uint8Array(12 + sps.length + pps.length
                                + (isHigh ? 3 : 0));
      var o = 0;
      desc[o++] = 1;                                   // configurationVersion
      desc[o++] = sps[1];                              // AVCProfileIndication
      desc[o++] = sps[2];                              // profile_compatibility
      desc[o++] = sps[3];                              // AVCLevelIndication
      desc[o++] = 0xff;                                // lengthSizeMinusOne=3
      desc[o++] = 0xe1;                                // numOfSPS（111 前缀+1）
      desc[o++] = (sps.length >> 8) & 255;
      desc[o++] = sps.length & 255;
      desc.set(sps, o); o += sps.length;
      desc[o++] = 0x01;                                // numOfPPS = 1（整字节）
      desc[o++] = (pps.length >> 8) & 255;
      desc[o++] = pps.length & 255;
      desc.set(pps, o); o += pps.length;
      if (isHigh) {
        desc[o++] = 0x01;                              // chroma_format_idc=1
        desc[o++] = 0x00;                              // bit_depth_luma_minus8
        desc[o++] = 0x00;                              // bit_depth_chroma_minus8
        desc[o++] = 0x00;                              // numOfSPSExt = 0
      }
      var codecStr = "avc1."
        + ("0" + sps[1].toString(16)).slice(-2)
        + ("0" + sps[2].toString(16)).slice(-2)
        + ("0" + sps[3].toString(16)).slice(-2);
      return { desc: desc.subarray(0, o), codecStr: codecStr };
    }

    /* HEVCDecoderConfigurationRecord（hvcC，ISO 14496-15 Annex E）。
     * [构造要点] general_profile_space/tier/profile_idc、compatibility、
     * constraint、level 全部从 **SPS 的 profile_tier_level（NAL header
     * 后第 1..12 字节）** 拷贝——H.265 的 codec 串（hev1.<pid>.<compat>.
     * L<level>.<constraint 尾字节>）也从同一处生成；缺数组/字段错位都会
     * "Failed to parse hvcC"。lengthSizeMinusOne=3（4 字节长度前缀）。
     * [实测] 本机 Intel UHD 770 硬解路径 hvc1/hev1 isConfigSupported=true。 */
    function buildHvcCAndCodec(vps, sps, pps) {
      var pid = sps[2] & 0x1f;
      var tier = (sps[2] >> 5) & 0x01;
      var space = (sps[2] >> 6) & 0x03;
      var compat = sps.subarray(3, 7);
      var constraint = sps.subarray(7, 13);
      var level = sps[13];
      var len = 23 + vps.length + sps.length + pps.length
                + 3 * 6;                      /* 3 个数组的 6 字节头 */
      var h = new Uint8Array(len);
      var o = 0;
      h[o++] = 1;                             // configurationVersion
      h[o++] = ((space & 3) << 6) | ((tier & 1) << 5) | (pid & 0x1f);
      h.set(compat, o); o += 4;               // general_profile_compatibility
      h.set(constraint, o); o += 6;           // general_constraint_indicator
      h[o++] = level;                         // general_level_idc
      h[o++] = 0xf0; h[o++] = 0x00;           // min_spatial_segmentation_idc
      h[o++] = 0xfc;                          // parallelismType = 0
      h[o++] = 0xf8 | 0x01;                   // chromaFormat = 1 (4:2:0)
      h[o++] = 0xf8 | 0x00;                   // bitDepthLumaMinus8 = 0
      h[o++] = 0xf8 | 0x00;                   // bitDepthChromaMinus8 = 0
      h[o++] = 0x00; h[o++] = 0x00;           // avgFrameRate = 0
      h[o++] = 0x0f;                          // 000000 01 1 1 11：
                                              // CFR=0, layers=1, nested=1,
                                              // lengthSizeMinusOne=3
      h[o++] = 0x03;                          // numOfArrays = 3
      var arrs = [[0x20, vps], [0x21, sps], [0x22, pps]];
      arrs.forEach(function (ent) {
        h[o++] = 0x80 | ent[0];               // array_completeness+type
        h[o++] = 0x00; h[o++] = 0x01;         // numOfNalus = 1
        h[o++] = (ent[1].length >> 8) & 255;
        h[o++] = ent[1].length & 255;
        h.set(ent[1], o); o += ent[1].length;
      });
      var codecStr = "hev1." + (pid & 0x1f) + "."
        + (compat[0] ? compat[0].toString(16) : "1") + ".L" + level
        + ".B0";
      return { desc: h.subarray(0, o), codecStr: codecStr };
    }

    function esFeed(nal, h265) {
      if (!es || !es.supported) return;
      try {
      var body = stripStartCode(nal);
      var t = nalType(nal, h265);
      /* ---- 参数集：攒齐后构造 desc 并 configure（H264/H265 双路）---- */
      if (isParam(t, h265)) {
        if (t === 7 || t === 33) es.sps = new Uint8Array(body);  // 拷贝（fetch 缓冲会复用）
        else if (t === 8 || t === 34) es.pps = new Uint8Array(body);
        else if (t === 32) es.vps = new Uint8Array(body);        // H.265 VPS
        var ready = h265 ? (es.sps && es.pps && es.vps)
                         : (es.sps && es.pps);
        if (ready && !es.configured) {
          var built = h265 ? buildHvcCAndCodec(es.vps, es.sps, es.pps)
                           : buildDescAndCodec(es.sps, es.pps);
          es.descErr = "";
        es.decoder = new VideoDecoder({
          output: function (vf) {
            es.frames++;
            /* [draw 异常必须暴露] 静默抛会让"解码在跑、画面永不出"
             * 成为最难查的形态（解码帧数在涨、界面还是 placeholder）。 */
            try {
              draw(vf);
            } catch (drawErr) {
              es.drawErr = String(drawErr).slice(0, 120);
              placeholder.style.display = "";
              canvas.style.display = "none";
              setText(placeholder.firstChild,
                      "画面绘制失败：" + es.drawErr);
            }
            vf.close();
          },
          error: function (e) {
            /* 解码器状态坏 ⇒ 全量重来（服务端会从参数集给起）。
             * 错误文本进 F12 排查口（__camEs.lastErr）。 */
            es.lastErr = String(e).slice(0, 140);
            setText(statusLine, "解码出错，重新同步…");
            esReset();
          },
        });
          /* [hev1↔hvc1 互换] 同一 desc 下两种封装标记都试（浏览器对
           * 二者的 desc 解析路径不同；实测 IAB hev1/hvc1 均硬件可用）。 */
          var tried = [built.codecStr];
          if (built.codecStr.indexOf("hev1.") === 0) {
            tried.push("hvc1." + built.codecStr.slice(5));
          } else if (built.codecStr.indexOf("hvc1.") === 0) {
            tried.push("hev1." + built.codecStr.slice(5));
          }
          for (var ci = 0; ci < tried.length && !es.configured; ci++) {
            try {
              es.decoder.configure({ codec: tried[ci],
                                     description: built.desc,
                                     optimizeForLatency: true });
              es.configured = true;
              es.codecStr = tried[ci];
            } catch (e) {
              es.descErr = String(e).slice(0, 80);
            }
          }
          if (!es.configured) {
            setText(statusLine, "解码配置失败：" + es.descErr);
            try { es.decoder.close(); } catch (e2) { /* ignore */ }
            es.decoder = null;
            es.sps = null; es.pps = null; es.vps = null;
          }
        }
        return;
      }
      /* [守卫] esReset 后（decoder=null、SPS/PPS 缓存保留）⇒ 等 IDR
       * 到来，在 IDR 分支内重建 decoder 自举；其余丢弃。 */
      if (!es.decoder && !es.sps) return;
      /* ---- IDR：key chunk = [SPS][PPS][IDR]（首帧自举带参数集）---- */
      if (isIdr(t, h265)) {
        /* [esReset 后自举] 真相机参数集只发一次 ⇒ esReset **保留** SPS/PPS
         * 缓存；IDR 到来时用缓存重建 decoder（configure + key chunk）。
         * decoder 还在（configured）⇒ key chunk 只拼 IDR。 */
        if (!es.decoder && es.sps && es.pps) {
          var built2 = buildDescAndCodec(es.sps, es.pps);
          es.decoder = new VideoDecoder({
            output: function (vf) {
              es.frames++;
              /* [draw 异常必须暴露] 静默抛会让"解码在跑、画面永不出"
               * 成为最难查的形态（解码帧数在涨、界面还是 placeholder）。 */
              try {
                draw(vf);
              } catch (drawErr) {
                es.drawErr = String(drawErr).slice(0, 120);
                placeholder.style.display = "";
                canvas.style.display = "none";
                setText(placeholder.firstChild,
                        "画面绘制失败：" + es.drawErr);
              }
              vf.close();
            },
            error: function (e) {
              /* 解码器状态坏 ⇒ 全量重来（错误文本进 __camEs.lastErr）。 */
              es.lastErr = String(e).slice(0, 140);
              setText(statusLine, "解码出错，重新同步…");
              esReset();
            },
          });
          try {
            es.decoder.configure({ codec: built2.codecStr,
                                   description: built2.desc,
                                   optimizeForLatency: true });
            es.configured = true;
            es.codecStr = built2.codecStr;
          } catch (e) {
            try { es.decoder.close(); } catch (e2) { /* ignore */ }
            es.decoder = null;
            return;
          }
        }
        var bodies = [];
        if (es.vps) bodies.push(es.vps);
        if (es.sps) bodies.push(es.sps);
        if (es.pps) bodies.push(es.pps);
        bodies.push(body);
        es.waitKey = false;
          try {
          es.decoder.decode(new EncodedVideoChunk({
            type: "key", timestamp: es.frames * 50000,
            data: avccPack(bodies) }));
          es.frames++;
        } catch (e) {
          es.dropped++;
          esReset();                /* key 都解不动 ⇒ 全量重新同步 */
        }
        return;
      }
      /* ---- 非 IDR VCL：delta ---- */
      if (es.waitKey) return;       /* 还没见首 IDR，P 帧无法解 */
      try {
        es.decoder.decode(new EncodedVideoChunk({
          type: "delta", timestamp: es.frames * 50000,
          data: avccPack([body]) }));
        es.frames++;
      } catch (e) {
        es.dropped++;
        if (es.dropped % 30 === 1) esReset();   /* 丢片成串 ⇒ 重新同步 */
      }
      } catch (e) {
        dbg("FEED-ERR:" + String(e).slice(0, 80));
      }
    }

    /* [404 退避 2026-11-19 性能项] 相机离线/未启用时 ES 端点恒 404，
     * 120ms 一次的空轮询是纯浪费 ⇒ 连续 404 计数 >5 ⇒ 间隔退避到
     * 500ms；拿到数据立即恢复 120ms（画面延迟优先）。 */
    var ES_FETCH_BACKOFF_MS = 500;
    function esPoll() {
      if (!es || !es.supported || es.busy) return;
      if (es.miss404 >= 5 && Date.now() - es.lastTry < ES_FETCH_BACKOFF_MS) {
        return;
      }
      es.busy = true;
      es.lastTry = Date.now();
      var h265 = codec === "h265";
      fetch(ES_URL + "?cursor=" + es.cursor, { credentials: "same-origin" })
        .then(function (r) {
          if (r.status === 404) {
            es.busy = false;
            es.miss404 = (es.miss404 || 0) + 1;
            return null;
          }
          es.miss404 = 0;
          return r.arrayBuffer();
        })
        .then(function (buf) {
          es.busy = false;
          try {
          if (!buf) return;
          var dv = new DataView(buf);
          var off = 0;
          var cur = dv.getUint32(off, true); off += 4;
          /* u16 codecFlag, u16 resync */
          var resync = dv.getUint16(off + 2, true) === 1; off += 4;
          var count = dv.getUint32(off, true); off += 4;
          if (resync) {
            /* 游标被环形缓冲挤掉 ⇒ 全量重放（先重置解码器与参数集） */
            if (es.decoder) es.decoder.reset();
            es.sps = null; es.pps = null; es.vps = null;
            es.configured = false;
            es.cursor = 0;
            esNeedKey();
          }
          es.cursor = Math.max(es.cursor, cur);
          for (var i = 0; i < count; i++) {
            var len = dv.getUint32(off, true); off += 4;
            var nal = new Uint8Array(buf, off, len); off += len;
            esFeed(nal, h265);
          }
          /* [5 秒无帧提示] configure 成功但解码器不出帧（个别内核/驱动
           * 环境）⇒ 给可操作建议，不黑屏装死（一次性）。带 lastErr
           * （解码 error 回调文本）⇒ F12 都不用开。 */
          if (es.configured && es.frames === 0) {
            if (!es.firstCfgTs) es.firstCfgTs = Date.now();
            else if (Date.now() - es.firstCfgTs > 5000 && !es.noOutputTip) {
              es.noOutputTip = true;
              placeholder.style.display = "";
              canvas.style.display = "none";
              setText(placeholder.firstChild,
                      "当前浏览器解不出 H.264 直传画面（解码器无输出"
                      + (es.lastErr ? "：" + es.lastErr : "") + "）："
                      + "请换 Chrome/Edge 正式版，或在远端设置→相机→传输"
                      + "切回「JPEG 转发」");
              setText(statusLine, "解码无输出");
            }
          } else if (es.frames > 0) {
            es.noOutputTip = false;
          }
          var doc = camDoc();
          setText(statusLine, "画面接收中（" + (codec === "h265" ? "H.265" : "H.264")
                  + " 直传 · " + (es.decoder && es.decoder.state === "configured"
                  ? es.decoder.state : "…") + "）");
          setText(metaLine, "帧 " + es.frames + " · 编码 "
                  + (codec === "h265" ? "H.265" : "H.264")
                  + " · 更新 " + fmtTime(Date.now())
                  + (es.dropped ? " · 丢 " + es.dropped : ""));
          } catch (pollErr) {
            dbg("POLL-ERR:" + String(pollErr).slice(0, 100));
          }
        })
        .catch(function (e) {
          es.busy = false;
          dbg("FETCH-ERR:" + String(e).slice(0, 80));
        });
    }

    function renderAll() {
      var doc = camDoc();
      var newMode = doc.mode || "jpeg";
      var newCodec = doc.codec || "h264";
      if (newMode === "es" && mode !== "es") {
        mode = "es"; codec = newCodec;
        img.style.display = "none";
        esStart(newCodec === "h265");
      } else if (newMode === "jpeg" && mode !== "jpeg") {
        /* 服务端切回 jpeg（配置改 transport）⇒ 拆掉 es 播放器 */
        mode = "jpeg";
        if (es && es.decoder) { try { es.decoder.close(); } catch (e) {} }
        es = null;
        canvas.style.display = "none";
        lastSeq = -1;
      }
      if (mode === "es") {
        setText(statusLine, es && es.supported ? "画面接收中（直传）" : "无法解码");
        return;
      }
      refreshImage();
      renderMetaJpeg(doc);
    }

    /* ---- 订阅与定时器 ---- */
    unsubs.push(store.on("camera", function () {
      renderAll();
      if (mode === "es") setText(statusLine, "画面接收中（直传）");
    }));
    unsubs.push(store.on("link", renderAll));

    timers.push(setInterval(function () {
      var doc = camDoc();
      if (doc.mode === "es") {
        if (mode === "es") esPoll();
        return;
      }
      /* jpeg 兜底：store 没变但服务端可能已推新帧 */
      if (doc.hasFrame && (doc.seq || 0) !== lastSeq) refreshImage();
      renderMetaJpeg(doc);
    }, POLL_MS));
    timers.push(setInterval(function () {
      if (mode === "es") esPoll();
    }, ES_FETCH_MS));

    /* [订阅自愈看门狗 2026-11-23 用户实报「相机悬浮窗画面不会自己刷新」]
     * 现象：SSE 活着（别的卡片都在变），只有相机画面冻在最后一帧。
     * 根因：上游侧的订阅表可能被清（web/上游重启窗口、会话复位），而
     * store 的"同值早退"让后续都不会再上报 ⇒ 永不恢复。
     * 判据：链路就绪 ∧ store 里 camera.seq 连续 5s 没变 ⇒ 强制重发主题
     * （10s 节流，别把 /api/topics 打成心跳）。相机真离线时重发也无害。
     * 放在 mountPage 里 ⇒ **相机页与标题栏悬浮卡同时受益**。 */
    var wdSeq = -1;
    var wdAt = 0;
    var wdRepushAt = 0;
    timers.push(setInterval(function () {
      var doc = camDoc();
      var seq = doc.seq || 0;
      var now = Date.now();
      if (seq !== wdSeq) { wdSeq = seq; wdAt = now; return; }
      var link = store.get("link") || {};
      if (link.ready === false) return;
      if (now - wdAt < 5000 || now - wdRepushAt < 10000) return;
      wdRepushAt = now;
      if (HSF.store.repushTopics) HSF.store.repushTopics();
    }, 2000));

    renderAll();

    return function cleanup() {
      unsubs.forEach(function (fn) { if (fn) fn(); });
      timers.forEach(function (t) { clearInterval(t); });
      if (es && es.decoder) { try { es.decoder.close(); } catch (e) {} }
      es = null;
      if (window.__camEs) delete window.__camEs;
    };
  }

  /* [悬浮相机卡复用同一套取流/解码逻辑 2026-11-22] 返回 cleanup；
   * compact=true 只建画面区（无页头/状态行/全屏钮）。
   * [只在卡片存在期间有流] 调用方负责在开卡时给 store 加相机主题、
   * 关卡时移除 —— 关闭后上游停止推帧（用户口径：别后台一直传大流量）。 */
  HSF.cameraViewMount = function (host, compact) {
    return mountPage(host, !!compact);
  };
})();
