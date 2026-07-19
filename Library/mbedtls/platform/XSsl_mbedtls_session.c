// XSsl_mbedtls_session.c
// Copyright (C) 2026 Your Project Authors
// SPDX-License-Identifier: MIT OR LGPL-3.0-only
//
// XSsl 濠电姷鏁搁崑鐐差焽濞嗘挸瑙﹂悗锝庡亞閳瑰秵绻涘顔荤凹闁稿骸锕弻锝夋晲閸涱収娈┑鐐茬焾娴滎亪寮诲☉姘勃闁告挆鍛帒闂?mbedTLS 闂傚倸鍊风粈渚€骞夐敓鐘冲殞闁诡垼鐏愯ぐ鎺撳€荤紒娑橆儐閺呮粌顪冮妶鍡楃瑨閻庢凹鍓濈换姘舵⒒娴ｄ警鐒鹃柡鍫墴閹柉顦归柟顖氳嫰閻ｆ繈宕熼鍌氬箞闂備焦瀵ч弻銊╁箹椤愶絼绻嗛柤娴嬫櫇绾惧ジ寮堕崼娑樺闁诲繐顕埀?XSSL_USE_MBEDTLS 闂傚倸鍊风粈渚€骞栭锕€鐤い鎰剁稻濞呯娀骞栨潏鍓хɑ妞ゎ偅娲橀妵鍕疀閹炬惌妫￠柣搴㈢瀹€鎼佸蓟閻旇　鍋撳☉娆樼劷闁规彃娼￠弻?//
// 濠电姷鏁搁崑鐐哄箰閼姐倕鏋堢€广儱娲﹀畷鏌ユ煕閳╁啰鎳呴柣顓烆樀閺屾盯鍩勯崘鐐吂濠电偛鐡ㄩ悧鐘诲蓟閺囩喎绶炴繛鎴炴皑閺嗙姵绻濋姀锝嗙【缂佸缍婂?//   - 闂傚倸鍊风粈渚€骞夐敓鐘茬闁哄洢鍨圭粻鐘诲箹濞ｎ剙濡介柛? XMemory (XCalloc_Hybrid / XFree_Hybrid via 婵犲痉鏉库偓妤佹叏閻戣棄纾绘繛鎴欏灩閻ゎ噣鏌ら幇浣哥仜濞存粌缍婇幃妤呮偨閸涘﹥鐝冲┑?
//   - 闂傚倸鍊搁崐鎼佸磹閹间礁绠犻幖杈剧稻瀹曟煡鏌熺€涙濡囬柡鈧敃鍌涚厓? mbedTLS 闂傚倸鍊风粈渚€骞夐敓鐘茬闁哄洢鍨圭粻鐘虫叏濡炶浜鹃悗瑙勬礃濞叉粓鍩€椤掑嫭娑ч柟璇х磿娴?PSA闂傚倸鍊烽悞锔锯偓绗涘懐鐭欓柟杈惧瘜閺佸棙銇勯妸褜鍎?闂?XRandomGenerator闂傚倸鍊烽悞锔锯偓绗涘懐鐭欓柟杈鹃檮閸嬪鏌涢埄鍐炬闁?XSsl_mbedtls.c 闂傚倸鍊搁崐鐑芥倿閿曚降浜归柛鎰靛枛鐎氬銇勯幒鎴濃偓鐢稿磻閹捐鍨傛い鎰剁悼椤︿即姊洪崫鍕効缂佺姵鎹囧畷娲晬閸曘劌浜鹃柨婵嗛婢х増銇?
//   - 濠电姷鏁搁崑鐔妓夐幇鏉跨；闁归偊鍘介崣蹇涙煕閹炬瀚烽崑? 濠电姷鏁告慨浼村垂婵傜鏄ラ柡宥庡幗閸嬪鏌ｅΟ娆惧殭缁?BIO 闂傚倸鍊烽悞锕傚箖閸洖纾块柟鎯版绾惧鏌ｅΟ鑲╁笡闁稿骸绉归弻锝夊棘閸喗鍊梺鍝勬媼閸撶喖寮诲☉銏╂晝闁绘灏欐禒楣冩⒑閸濆嫭顥滅紒缁橈耿瀵鎮㈤悡搴ｎ啋濡炪倖妫佹慨銈夊汲閻樼粯鍋℃繝濠傜墱閺嗘帡鏌ｉ幒鐐差洭缂侇噮鍙冮幃銏焊娴ｈ娼旈柣搴ゎ潐濞叉牕煤閵娾敡?

#include "XSsl_session.h"

#ifdef XSSL_USE_MBEDTLS

/* NOTE(mbedtls 4.x): the public API does not expose alert msg_callback nor
 * session-ticket receive callback, so XSslSocket_alertSent/alertReceived/
 * newSessionTicketReceived signals cannot be wired here. They remain
 * emittable externally; once the OpenSSL backend lands, revisit. */


#include "XSsl_mbedtls_p.h"
#include "XMemory.h"
#include <mbedtls/ssl.h>
#include <mbedtls/x509_crt.h>
#include <mbedtls/pk.h>
#include <mbedtls/error.h>
#include <string.h>
#include "XPrintf.h"
#include "XVector.h"

 /* ---- 闂傚倸鍊风粈渚€骞夐敓鐘茬闁哄洢鍨圭粻鐘虫叏濡炶浜鹃悗瑙勬礃濞叉粓鍩€椤掍胶鈯曢柨姘亜閺囷繝鍝洪柟渚垮妼铻ｉ柧蹇曟缁辩偛鈹戦埥鍡楃仩闁绘绻掑Σ鎰板箻鐎涙ê顎撻柣鐔哥懃鐎氼剟妫勫澶嬧拺?----------------------------------------------------- */
struct XSslSession {
    mbedtls_ssl_context     ssl;
    mbedtls_ssl_config      conf;
    mbedtls_x509_crt        ca_chain;   /* 闂傚倸鍊风粈渚€骞夐敓鐘冲仭妞ゆ牜鍋涢崹鍌炴⒑椤掆偓缁夌敻宕戠€ｎ喖绠规繛锝庡墮閻掔儤绻涢崼婊呯煓闁诡喗锕㈤幃娆撴偨閸偅鍟掗梻浣规偠閸婃鎱ㄦ搴㈩潟闁圭儤顨呯猾宥夋煙鐎涙绠樺ù鐙€鍨伴埞鎴︽倷閼碱剙顤€闂佹悶鍔屾晶搴ｅ垝濮樿埖鐒肩€广儱鎳愭导瀣⒑閸涘﹦鎳冩い锔诲灡閹梹绻濋崶銊㈡嫽?*/
    bool                    ca_inited;

    /* BIO */
    void* bio_user;
    XSslBioSend             bio_send;
    XSslBioRecv             bio_recv;

    /* 闂傚倸鍊烽懗鍓佸垝椤栫偐鈧箓宕奸妷銉︽К闂佸搫绋侀崢濂告倿?*/
    bool                    is_server;
    bool                    encrypted;
    XSslPeerVerifyMode      verify_mode;
    bool                    setup_done;

    /* 闂備浇顕х€涒晠顢欓弽顓炵獥闁哄诞鍛濡炪倖甯掗崐褰掞綖閺囥垺鐓冮柛婵嗗閸ｆ椽鏌涚€ｃ劌濮傞柡灞炬礃缁绘盯宕归鐓幮ゆ繝纰樻閸ㄦ澘螞濠靛棭娼栨繛宸憾閺佸﹪鏌涘┑鍡楊仼妞ゎ剙鐗嗛—鍐Χ閸℃ê顦╅梺鍝ュУ瀹€鎼佺嵁閹版澘绠虫俊銈傚亾闁绘搫绻濋弻娑樷槈閸楃偟浠銈忚吂閺呮粎鎹㈠☉銏犵闁绘劖娼欑喊宥囩磽娴ｅ壊妲洪柡浣规倐閸┾偓妞ゆ帊绀侀幖鎼佹煕閵娿劍顥夋い鏇秮瀹曞綊顢曢姀銏㈢嵁闂佽鍑界紞鍡樼閸洖姹查柨鐔哄У閳锋垿鏌涘┑鍡楊伀濠⒀勬礃閵囧嫰鏁傞懞銉у嚒濡炪値鍘煎ú顓炵暦婵傜唯闁挎棁濮ら悵顐︽⒑鐠囪尙绠抽柛瀣█瀹曟垿骞囬悧鍫濆壒闁诲繒鍋涢～鏇㈠焵?mbedtls 濠电姷鏁搁崑鐘诲箵椤忓棛绀婇柍褜鍓氱换娑欏緞鐎ｎ偆顦伴悗娈垮櫘閸嬪﹥淇婇懜闈涚窞濠电姴瀚峰Σ顖炴⒑閼姐倕孝闁圭⒈鍋婇獮鏍敃閿曗偓閺嬩線鏌涢锝嗙闁抽攱鍨堕妵鍕棘閹稿孩鍎撴繝纰樷偓鐐藉仮闁哄矉缍侀崺濠傗枎韫囨挻娈滈梺鍝勵儐濮婂鎯€椤忓牆绾ф繛鍡欏亾妤旂紓鍌欒閸?*/
    XSslCertificate* own_cert;
    XSslKey* own_key;

    /* 闂傚倸鍊风粈渚€骞栭锔藉亱闁告劦鍠栫壕濠氭煙閹规劦鍤欑紒鐙欏洦鐓冮柛婵嗗閳ь剚鎮傞幃姗€鏁愰崶鈺冿紲闂佸搫鍟犻崑鎾寸箾閸忚偐鎳囬柛鈹垮灪閹棃濡搁敂鑺ヮ仧闂備胶绮…鍫濃枍閺囩偐鏋嶉柟鍓х帛閻撶喖鏌ｅΟ鍝勭骇缂佷讲鏅犻弻娑㈠Ω閵夈儲姣愰梺宕囩帛閹瑰洭鐛€ｎ喗鏅濋柍褜鍓熼幆灞轿旀担鍏哥盎闂佸搫绉查崝搴ｇ不閵夆晜鐓曢柕鍫濆暙閻忔煡鏌″畝瀣瘈鐎规洘甯掗～婵嬵敇閻戝棙袩濠电姷鏁告慨顓㈠磻?*/
    char                    err_buf[128];
};

static XSslProtocol s_proto_of(XSslProtocol p) { return p; }

static int map_bio_send(void* ctx, const unsigned char* buf, size_t len) {
    XSslSession* s = (XSslSession*)ctx;
    if (!s->bio_send) return MBEDTLS_ERR_SSL_INTERNAL_ERROR;
    int r = s->bio_send(s->bio_user, buf, len);
    if (r >= 0) return r;
    if (r == XSSL_BIO_WANT_WRITE) return MBEDTLS_ERR_SSL_WANT_WRITE;
    return MBEDTLS_ERR_SSL_INTERNAL_ERROR;
}
static int map_bio_recv(void* ctx, unsigned char* buf, size_t len) {
    XSslSession* s = (XSslSession*)ctx;
    if (!s->bio_recv) return MBEDTLS_ERR_SSL_INTERNAL_ERROR;
    int r = s->bio_recv(s->bio_user, buf, len);
    if (r > 0) return r;
    if (r == 0) return MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY;
    if (r == XSSL_BIO_WANT_READ) return MBEDTLS_ERR_SSL_WANT_READ;
    return MBEDTLS_ERR_SSL_INTERNAL_ERROR;
}

static int session_map_result(XSslSession* s, int r) {
    if (r == 0) return XSSL_S_OK;
    if (r == MBEDTLS_ERR_SSL_WANT_READ) return XSSL_S_WANT_READ;
    if (r == MBEDTLS_ERR_SSL_WANT_WRITE) return XSSL_S_WANT_WRITE;
    if (r == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY) return XSSL_S_CLOSED;
    /* 闂傚倷娴囧畷鍨叏閹惰姤鍊块柨鏇楀亾妞ゎ厼鐏濊灒闁兼祴鏅濋ˇ顖炴倵楠炲灝鍔氭い锔垮嵆閹繝濡烽敂鍓ь啎闂佺硶鍓濋敋濞ｅ浂鍨堕弻娑㈠Χ閸℃瑥鈪归梺?*/
    mbedtls_strerror(r, s->err_buf, sizeof(s->err_buf));
    XPrintf("[XSsl] mbedtls err %d (-0x%04x): %s\n", r, (unsigned)(-r), s->err_buf);
    return XSSL_S_ERROR;
}

/* 濠电姷顣藉Σ鍛村磻閹捐泛绶ゅΔ锝呭暞閸嬪淇婇妶鍛闁轰礁鍟撮弻娑滅疀閹捐櫕鍊繝娈垮灡閹告娊骞冭ぐ鎺戠畳闁圭儤鍨甸‖澶愭⒑閸濆嫭顥滅紒缁樺姍濠€渚€姊虹粙璺ㄧ婵☆偅瀵х粋宥夊閵堝棛鍘介梺鎸庢濞夋洟鎯屽▎鎴斿亾鐟欏嫭绀冪紒顔肩焸椤㈡﹢鎳濋幍顔炬澑闂佸搫娲ㄦ刊顓炍ｉ悜鑺モ拻濞达絿鐡旈崵鍐煕閵娿劍鐝柡鍛版硾铻栭柛娑卞幘閸旓箑顪冮妶鍡楃劸闁告挻鐟ヨ灋婵炴垶菤閺嬫棃鎮规ウ瑁も偓鈧俊鎻掔墦閺岀喓绱掗姀鐘典哗婵炲瓨绮嶉崕鎶藉煘?XSsl_platform_init闂傚倸鍊烽悞锔锯偓绗涘懐鐭欓柟鐑橆殕閸ゅ苯螖閿濆懎鏆為柛瀣ф櫊閺屾洘绻涜鐎氼剟寮查幎鑺モ拺闁告稑锕ｇ欢閬嶆煕濡亽鍋㈢€殿噮鍋婂畷濂稿即閻斿皝鍋?hook 婵? * 闂傚倸鍊风粈渚€骞夐敍鍕殰婵°倕鍟畷鏌ユ煕瀹€鈧崕鎴犵礊閺嶎厽鐓欓柣妤€鐗婄欢鑼磼閳?PSA crypto闂傚倸鍊风欢姘焽瑜嶈灋闁哄啫鐗嗙粻鐐电磼鐏炲鍋tls 4.x TLS 濠电姷鏁搁崑鐐哄箰閼姐倕鏋堢€广儱娲﹀畷鏌ユ煕閳╁啰鎳呴柣?PSA闂傚倸鍊烽悞锔锯偓绗涘懐鐭欓柟瀵稿仧闂勫嫰鏌￠崘銊モ偓鑽ょ不閺傛鐔嗛悹铏瑰劋濠€浼存倵濮樼厧澧撮柡灞剧洴楠炲洭濡搁敂鐣屽綗闂備礁鎲￠弻銊╁疮閹绢喖钃熸繛鎴欏灩缁狅綁鏌ㄥ┑鍡樺晽闁瑰墽绮悡娑㈡煕閳╁啰鎳冮柡瀣⊕椤ㄣ儵鎮欑€涙ê纾抽梺绯曟櫅鐎氭澘鐣峰鈧垾锕傚箣閻愬鐣梻鍌氬€搁…顒勫磻閸曨個娲Χ婢跺﹦鐤囬柟鍏肩暘閸斿本顢婇梻浣烘嚀椤曨厽鎱ㄩ悽纰樺亾濮橆剦鐓奸柡灞炬礃瀵板嫬鈽夊顒傜厳闂?*/
static void xssl_ensure_platform(void) {
    static int inited = 0;
    if (inited) return;
    if (XSsl_platform_init()) { inited = 1; }
    else { XPrintf("[XSsl] XSsl_platform_init FAILED\n"); }
}

/* ---- 闂傚倸鍊烽悞锕傛儑瑜版帒鍨傚┑鐘宠壘缁愭骞栧ǎ顒€濡肩紒鐘靛█閺屻劑鎮㈤崫鍕戙垻鈧懓鎲＄换鍫ュ蓟閺囩喓绡€闊洦绋掗宥夋⒑?--------------------------------------------------------- */
XSslSession* XSsl_sessionCreate(XSslProtocol protocol, bool isServer) {
    xssl_ensure_platform();
    XSslSession* s = (XSslSession*)XMalloc_System(sizeof(XSslSession));
    if (!s) return NULL;
    memset(s, 0, sizeof(*s));

    mbedtls_ssl_init(&s->ssl);
    mbedtls_ssl_config_init(&s->conf);
    mbedtls_x509_crt_init(&s->ca_chain);
    s->is_server = isServer;
    s->verify_mode = XSSL_AutoVerifyPeer;

    int endpoint = isServer ? MBEDTLS_SSL_IS_SERVER : MBEDTLS_SSL_IS_CLIENT;
    int transport = MBEDTLS_SSL_TRANSPORT_STREAM; /* DTLS 闂傚倸鍊风粈渚€骞夐敓鐘冲殞闁诡垼鐏愯ぐ鎺撳€婚柤鎭掑劚娴犵儤绻濋悽闈浶ｉ柤鐟板⒔缁濡烽埡鍌滃帗闁哄鍋炴刊浠嬪礂鐏炲墽绠?*/

    if (mbedtls_ssl_config_defaults(&s->conf, endpoint, transport,
        MBEDTLS_SSL_PRESET_DEFAULT) != 0) {
        goto fail;
    }
    /* TLS 闂傚倸鍊烽懗鍓佸垝椤栫偑鈧啴宕ㄧ€涙ê浜辨繝鐢靛Т閸嬪﹪鎳?*/
    /* TLS version selection: XSSL_TlsV1_x -> exact, XSSL_TlsV1_xOrLater -> min only,
     * XSSL_SecureProtocols/XSSL_AnyProtocol -> [TLS1_2, TLS1_2] until TLS1_3 backend is complete. */
    int vmin = MBEDTLS_SSL_VERSION_TLS1_2;
    int vmax = MBEDTLS_SSL_VERSION_TLS1_2;
    bool has_max = true;
    switch (protocol) {
    case XSSL_TlsV1_2:        vmin = vmax = MBEDTLS_SSL_VERSION_TLS1_2; has_max = true;  break;
    case XSSL_TlsV1_3:        vmin = vmax = MBEDTLS_SSL_VERSION_TLS1_3; has_max = true;  break;
    case XSSL_TlsV1_2OrLater: vmin = MBEDTLS_SSL_VERSION_TLS1_2;        has_max = false; break;
    case XSSL_TlsV1_3OrLater: vmin = MBEDTLS_SSL_VERSION_TLS1_3;        has_max = false; break;
    case XSSL_AnyProtocol:
    case XSSL_SecureProtocols:
        /* TODO(mbedtls 4.x): TLS1.3 ClientHello triggers -0x6600 on some servers (e.g. baidu). Cap at 1.2 until upstream fix. */
    default:                  vmin = MBEDTLS_SSL_VERSION_TLS1_2; vmax = MBEDTLS_SSL_VERSION_TLS1_2; has_max = true; break;
    }
    mbedtls_ssl_conf_min_tls_version(&s->conf, vmin);
    if (has_max) mbedtls_ssl_conf_max_tls_version(&s->conf, vmax);
    /* AutoVerify: client REQUIRED, server NONE */
    if (isServer) {
        mbedtls_ssl_conf_authmode(&s->conf, MBEDTLS_SSL_VERIFY_NONE);
    }
    else {
        mbedtls_ssl_conf_authmode(&s->conf, MBEDTLS_SSL_VERIFY_REQUIRED);
    }
    (void)s_proto_of;
    return s;
fail:
    mbedtls_x509_crt_free(&s->ca_chain);
    mbedtls_ssl_config_free(&s->conf);
    mbedtls_ssl_free(&s->ssl);
    XFree_System(s);
    return NULL;
}

void XSsl_sessionDestroy(XSslSession* s) {
    if (!s) return;
    mbedtls_ssl_free(&s->ssl);
    mbedtls_ssl_config_free(&s->conf);
    mbedtls_x509_crt_free(&s->ca_chain);
    XFree_System(s);
}

/* ---- 闂傚倸鍊搁崐鐑芥倿閿曗偓椤灝螣閼测晝鐓嬮梺鍓插亝濞叉﹢宕?------------------------------------------------------------- */
void XSsl_sessionSetBio(XSslSession* s, void* user, XSslBioSend send_cb, XSslBioRecv recv_cb) {
    if (!s) return;
    s->bio_user = user;
    s->bio_send = send_cb;
    s->bio_recv = recv_cb;
    mbedtls_ssl_set_bio(&s->ssl, s, map_bio_send, map_bio_recv, NULL);
}

bool XSsl_sessionSetHostname(XSslSession* s, const char* hostname) {
    if (!s) return false;
    if (!hostname) {
#if defined(MBEDTLS_SSL_SERVER_NAME_INDICATION)
        return mbedtls_ssl_set_hostname(&s->ssl, NULL) == 0;
#else
        return true;
#endif
    }
#if defined(MBEDTLS_SSL_SERVER_NAME_INDICATION)
    return mbedtls_ssl_set_hostname(&s->ssl, hostname) == 0;
#else
    (void)hostname; return true;
#endif
}

bool XSsl_sessionSetCertificate(XSslSession* s, XSslCertificate* cert, XSslKey* key) {
    if (!s || !cert || !key) return false;
    s->own_cert = cert;
    s->own_key = key;
    return mbedtls_ssl_conf_own_cert(&s->conf, &cert->crt, &key->pk) == 0;
}

bool XSsl_sessionAddCaCertificate(XSslSession* s, XSslCertificate* ca) {
    if (!s || !ca) return false;
    /* 闂傚倸鍊烽懗鍫曞磿閻㈢鐤炬繛鎴欏灪閸嬨倝鏌曟繛褍瀚▓浼存⒑閸︻叀妾搁柛鐘崇墱缁?XSslCertificate 闂傚倸鍊风粈渚€骞夐敓鐘茬闁哄洢鍨圭粻鐘虫叏濡炶浜鹃悗瑙勬礃濞茬喖鐛鈧、娆撴寠婢跺﹤顥?x509 闂傚倸鍊搁崐鐑芥倿閿曞倸纾块悗鍦О娴滃綊鏌ｉ幇顔芥毄缂佲偓婵犲洦鐓涢柛鎰剁到娴滃墽绱?CA 闂傚倸鍊搁崐鐑芥嚄閸洖纾婚柟鎹愵嚙绾惧潡鏌熺捄鍝勵棡闁搞儯鍔庣弧鈧┑顔斤供閸橀箖宕濋幖浣光拺闂傚牊渚楅悞楣冩煕鎼达紕锛嶇紒顔藉哺瀹曠螖娴ｅ搫骞楁俊鐐€ら崑鎺楀礈濞嗘劒绻嗛柟闂寸劍閻?     * 闂傚倷娴囧畷鍨叏閹绢噮鏁勯柛娑欐綑閻ゎ喖霉閸忓吋缍戦柡瀣╃窔閺屾洟宕煎┑鎰﹀┑鈽嗗亝閿曘垹顫忓ú顏嶆晝闁挎繂娲㈤埀顒佸笧缁辨帡鎮╂潏鈺冪厯闂?ca 闂傚倸鍊烽悞锕傛儑瑜版帒鍨傚┑鐘宠壘缁愭骞栧ǎ顒€濡肩紒鐘靛█閺屻劑鎮㈤崫鍕戙垻鈧懓鎲＄换鍫ュ蓟閺囩喓绡€闊洦绋掗宥夋⒑缁洘娅呴柛濠傛健楠炲啴鎮滈挊澶婂祮闂佺粯鍔栭幆灞轿涢妶鍛斀闁绘劖娼欑徊濠氭煥閺囥劋绨婚柣锝呭槻椤繈顢橀妸褏鐓戝┑鐐舵彧缂嶁偓婵炲拑缍侀幃浼村Ψ閳哄倻鍘介梺缁樏鑸靛緞閸曨厾纾奸悹鍥皺婢э妇鈧鍠撻崝鎴炴叏閳ь剟鏌ｅΟ绨ф垹鏁Δ鍛闁告侗鍨伴弸鍫熶繆椤栨繃銆冨瑙勬礋濮婅櫣绮欓幐搴㈡嫳闂佸憡鍨归幊鎾圭亽闁诲函缍嗛崑鍡欑不妤ｅ啯鐓曢柣妯烘噺濠㈡﹢鍩€椤掍緡娈旈柍缁樻崌瀹曞ジ寮撮悢鍝勫箺婵＄偑鍊ら崑鎺楀礈濞嗘劒绻嗛柧蹇撴贡绾惧ジ寮堕崼娑樺妞ゃ儱顑夐弻鐔肩叓椤撶偛绁悗瑙勬礀瀹曨剟鍩㈡惔銊ョ疀妞ゆ洖妫涢惌妤佺節閻㈤潧浠﹂柛銊ョ埣瀵濡搁埡濠冩櫈闂佸憡渚楅崰妤€鈻嶉悩缁樼厽婵☆垵鍋愮敮娑㈡煕鎼达紕效闁哄本鐩鎾Ω閵夛妇褰梻浣告憸閸犲骸霉妞嬪孩顫曢柟鎹愵嚙缁€鍐煙椤栧棗瀚禍鏍⒒?*/
    s->ca_inited = true;
    mbedtls_ssl_conf_ca_chain(&s->conf, &ca->crt, NULL);
    return true;
}

void XSsl_sessionSetPeerVerify(XSslSession* s, XSslPeerVerifyMode mode) {
    if (!s) return;
    s->verify_mode = mode;
    int m = MBEDTLS_SSL_VERIFY_REQUIRED;
    switch (mode) {
    case XSSL_VerifyNone:     m = MBEDTLS_SSL_VERIFY_NONE;     break;
    case XSSL_QueryPeer:      m = MBEDTLS_SSL_VERIFY_OPTIONAL; break;
    case XSSL_VerifyPeer:     m = MBEDTLS_SSL_VERIFY_REQUIRED; break;
    case XSSL_AutoVerifyPeer: m = s->is_server ? MBEDTLS_SSL_VERIFY_NONE
        : MBEDTLS_SSL_VERIFY_REQUIRED; break;
    }
    mbedtls_ssl_conf_authmode(&s->conf, m);
}

/* ---- 闂傚倸鍊烽懗鍓佸垝椤栫偐鈧箓宕奸妷銉︽К闂佸搫绋侀崢濂告倿閸偁浜滈柟杈剧稻绾爼鏌涢弬璺ㄐｇ紒缁樼箚椤︻噣鏌?----------------------------------------------------------- */
static bool ensure_setup(XSslSession* s) {
    if (s->setup_done) return true;
    int r = mbedtls_ssl_setup(&s->ssl, &s->conf);
    if (r != 0) {
        mbedtls_strerror(r, s->err_buf, sizeof(s->err_buf));
        return false;
    }
    /* setup 濠电姷鏁搁崑鐐哄垂閸洖钃熼柕濞炬櫓閺佸嫰鏌涘☉娆愮稇缂佺姵鐗楁穱濠囧Χ閸涱喖娅ゅ銈冨劚濡繈寮诲鍫闂佸憡鎸鹃崰鏍ь嚕閼姐倓娌紒娑橆儏閹垿姊虹化鏇炲⒉妞ゎ厼娲畷銏ゅ箛椤斿墽锛濋梺绋挎湰閻熝囧礉瀹ュ鐓曢柕濞у嫭姣堥梺?BIO闂傚倸鍊烽悞锔锯偓绗涘懐鐭欓柟杈鹃檮閸嬪鏌涢埄鍐槈闁肩缍婇弻鐔虹磼閵忕姵鐏嶉梺缁樺浮缁犳牠寮婚弴鐔虹闁割煈鍠掗崑鎾澄旈崘顏嗗箵濠德板€曢幊蹇涘煕閹达附鐓欑紓浣姑粭褔鎳栭弽顓熲拺婵懓娲ら埀顒佹礀铻炴繝闈涱儏閺?*/
    mbedtls_ssl_set_bio(&s->ssl, s, map_bio_send, map_bio_recv, NULL);
    s->setup_done = true;
    return true;
}

int XSsl_sessionHandshake(XSslSession* s) {
    if (!s) return XSSL_S_ERROR;
    if (!ensure_setup(s)) return XSSL_S_ERROR;
    int r = mbedtls_ssl_handshake(&s->ssl);
    int mapped = session_map_result(s, r);
    if (mapped == XSSL_S_OK) s->encrypted = true;
    return mapped;
}

int XSsl_sessionRead(XSslSession* s, uint8_t* buf, size_t len) {
    if (!s || !buf || !len) return XSSL_S_ERROR;
    int r = mbedtls_ssl_read(&s->ssl, buf, len);
    if (r > 0) return r;
    /* WANT_READ/WANT_WRITE -> return 0 so caller does not confuse status
       code with legitimate 1/2-byte read counts. */
    if (r == MBEDTLS_ERR_SSL_WANT_READ) return 0;
    if (r == MBEDTLS_ERR_SSL_WANT_WRITE) return 0;
    if (r == 0) return 0;
    if (r == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY) { s->encrypted = false; return XSSL_S_CLOSED; }
    mbedtls_strerror(r, s->err_buf, sizeof(s->err_buf));
    XPrintf("[XSsl] read err %d (-0x%04x): %s\n", r, (unsigned)(-r), s->err_buf);
    return XSSL_S_ERROR;
}

int XSsl_sessionWrite(XSslSession* s, const uint8_t* buf, size_t len) {
    if (!s || !buf || !len) return XSSL_S_ERROR;
    int r = mbedtls_ssl_write(&s->ssl, buf, len);
    if (r > 0) return r;
    /* WANT_READ/WANT_WRITE -> 0 (would-block); do not collide with byte counts. */
    if (r == MBEDTLS_ERR_SSL_WANT_READ) return 0;
    if (r == MBEDTLS_ERR_SSL_WANT_WRITE) return 0;
    if (r == 0) return 0;
    if (r == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY) { s->encrypted = false; return XSSL_S_CLOSED; }
    mbedtls_strerror(r, s->err_buf, sizeof(s->err_buf));
    XPrintf("[XSsl] write err %d (-0x%04x): %s\n", r, (unsigned)(-r), s->err_buf);
    return XSSL_S_ERROR;
}

int XSsl_sessionShutdown(XSslSession* s) {
    if (!s) return XSSL_S_ERROR;
    int r = mbedtls_ssl_close_notify(&s->ssl);
    if (r == 0) return XSSL_S_OK;
    if (r == MBEDTLS_ERR_SSL_WANT_READ)  return XSSL_S_WANT_READ;
    if (r == MBEDTLS_ERR_SSL_WANT_WRITE) return XSSL_S_WANT_WRITE;
    /* Peer already gone / lower BIO error is expected during close; do not log. */
    return XSSL_S_CLOSED;

}
/* ---- 闂傚倸鍊风粈渚€骞栭銈嗗仏妞ゆ劧绠戠壕鍧楁煙缂併垹娅橀柡?-------------------------------------------------------------- */
bool XSsl_sessionIsEncrypted(const XSslSession* s) {
    return s ? s->encrypted : false;
}
uint32_t XSsl_sessionVerifyResult(const XSslSession* s) {
    if (!s) return (uint32_t)-1;
    return mbedtls_ssl_get_verify_result((mbedtls_ssl_context*)&s->ssl);
}
const char* XSsl_sessionProtocolString(const XSslSession* s) {
    if (!s) return "unknown";
    const char* v = mbedtls_ssl_get_version((mbedtls_ssl_context*)&s->ssl);
    return v ? v : "unknown";
}
const char* XSsl_sessionCipherName(const XSslSession* s) {
    if (!s) return NULL;
    return mbedtls_ssl_get_ciphersuite((mbedtls_ssl_context*)&s->ssl);
}
const char* XSsl_sessionLastErrorString(const XSslSession* s) {
    return s ? s->err_buf : "";
}



/* ---- Peer certificate access (Qt 6.8 alignment) ------------------------ */
XSslCertificate* XSsl_sessionPeerCertificate(XSslSession* s) {
    if (!s) return NULL;
    const mbedtls_x509_crt* c = mbedtls_ssl_get_peer_cert(&s->ssl);
    if (!c || !c->raw.p || c->raw.len == 0) return NULL;
    return XSsl_certificateFromDer(c->raw.p, c->raw.len);
}

XVector* XSsl_sessionPeerCertificateChain(XSslSession* s) {
    if (!s) return NULL;
    const mbedtls_x509_crt* c = mbedtls_ssl_get_peer_cert(&s->ssl);
    if (!c) return NULL;
    XVector* v = XVector_create(sizeof(XSslCertificate*));
    if (!v) return NULL;
    for (const mbedtls_x509_crt* it = c; it != NULL; it = it->next) {
        if (!it->raw.p || it->raw.len == 0) continue;
        XSslCertificate* one = XSsl_certificateFromDer(it->raw.p, it->raw.len);
        if (one) XVector_push_back_1_base(v, &one);
    }
    return v;
}

#endif /* XSSL_USE_MBEDTLS */

