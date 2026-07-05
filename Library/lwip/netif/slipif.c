#include "lwip/opt.h"

#include "netif/slipif.h"

#if LWIP_SLIP

#include "lwip/def.h"
#include "lwip/pbuf.h"
#include "lwip/stats.h"
#include "lwip/snmp.h"
#include "lwip/sys.h"
#include "lwip/sio.h"

#define SLIP_END     0xC0
#define SLIP_ESC     0xDB
#define SLIP_ESC_END 0xDC
#define SLIP_ESC_ESC 0xDD

#ifndef SLIP_MAX_SIZE
#define SLIP_MAX_SIZE 1500
#endif

#ifndef SLIP_SIO_SPEED
#define SLIP_SIO_SPEED(sio_fd) 0
#endif

enum slipif_recv_state {
  SLIP_RECV_NORMAL,
  SLIP_RECV_ESCAPE
};

struct slipif_priv {
  sio_fd_t sd;
  struct pbuf *p, *q;
  u8_t state;
  u16_t i, recved;
#if SLIP_RX_FROM_ISR
  struct pbuf *rxpackets;
#endif
};

static err_t
slipif_output(struct netif *netif, struct pbuf *p)
{
  struct slipif_priv *priv;
  struct pbuf *q;
  u16_t i;
  u8_t c;

  priv = (struct slipif_priv *)netif->state;
  sio_send(SLIP_END, priv->sd);

  for (q = p; q != NULL; q = q->next) {
    for (i = 0; i < q->len; i++) {
      c = ((u8_t *)q->payload)[i];
      switch (c) {
        case SLIP_END:
          sio_send(SLIP_ESC, priv->sd);
          sio_send(SLIP_ESC_END, priv->sd);
          break;
        case SLIP_ESC:
          sio_send(SLIP_ESC, priv->sd);
          sio_send(SLIP_ESC_ESC, priv->sd);
          break;
        default:
          sio_send(c, priv->sd);
          break;
      }
    }
  }
  sio_send(SLIP_END, priv->sd);
  return ERR_OK;
}

#if LWIP_IPV4
static err_t
slipif_output_v4(struct netif *netif, struct pbuf *p, const ip4_addr_t *ipaddr)
{
  LWIP_UNUSED_ARG(ipaddr);
  return slipif_output(netif, p);
}
#endif

#if LWIP_IPV6
static err_t
slipif_output_v6(struct netif *netif, struct pbuf *p, const ip6_addr_t *ipaddr)
{
  LWIP_UNUSED_ARG(ipaddr);
  return slipif_output(netif, p);
}
#endif

static struct pbuf *
slipif_rxbyte(struct netif *netif, u8_t c)
{
  struct slipif_priv *priv = (struct slipif_priv *)netif->state;
  struct pbuf *t;

  switch (priv->state) {
    case SLIP_RECV_NORMAL:
      switch (c) {
        case SLIP_END:
          if (priv->recved > 0) {
            pbuf_realloc(priv->q, priv->recved);
            t = priv->q;
            priv->p = priv->q = NULL;
            priv->i = priv->recved = 0;
            return t;
          }
          return NULL;
        case SLIP_ESC: priv->state = SLIP_RECV_ESCAPE; return NULL;
        default: break;
      }
      break;
    case SLIP_RECV_ESCAPE:
      switch (c) { case SLIP_ESC_END: c = SLIP_END; break; case SLIP_ESC_ESC: c = SLIP_ESC; break; default: break; }
      priv->state = SLIP_RECV_NORMAL; break;
    default: break;
  }

  if (priv->p == NULL) {
    priv->p = pbuf_alloc(PBUF_LINK, (PBUF_POOL_BUFSIZE - PBUF_LINK_HLEN - PBUF_LINK_ENCAPSULATION_HLEN), PBUF_POOL);
    if (priv->p == NULL) return NULL;
    if (priv->q != NULL) pbuf_cat(priv->q, priv->p); else priv->q = priv->p;
  }

  if ((priv->p != NULL) && (priv->recved <= SLIP_MAX_SIZE)) {
    ((u8_t *)priv->p->payload)[priv->i] = c; priv->recved++; priv->i++;
    if (priv->i >= priv->p->len) {
      priv->i = 0;
      if (priv->p->next != NULL && priv->p->next->len > 0) priv->p = priv->p->next;
      else priv->p = NULL;
    }
  }
  return NULL;
}

static void slipif_rxbyte_input(struct netif *netif, u8_t c) { struct pbuf *p = slipif_rxbyte(netif, c); if (p && netif->input(p, netif) != ERR_OK) pbuf_free(p); }

#if SLIP_USE_RX_THREAD
static void slipif_loop_thread(void *nf) { struct netif *netif = (struct netif *)nf; struct slipif_priv *priv = (struct slipif_priv *)netif->state; u8_t c; while (1) { if (sio_read(priv->sd, &c, 1) > 0) slipif_rxbyte_input(netif, c); } }
#endif

err_t slipif_init(struct netif *netif) {
  struct slipif_priv *priv; u8_t sio_num = LWIP_PTR_NUMERIC_CAST(u8_t, netif->state);
  priv = (struct slipif_priv *)mem_malloc(sizeof(struct slipif_priv)); if (!priv) return ERR_MEM;
  netif->name[0] = 's'; netif->name[1] = 'l';
#if LWIP_IPV4
  netif->output = slipif_output_v4;
#endif
#if LWIP_IPV6
  netif->output_ip6 = slipif_output_v6;
#endif
  netif->mtu = SLIP_MAX_SIZE;
  priv->sd = sio_open(sio_num); if (!priv->sd) { mem_free(priv); return ERR_IF; }
  priv->p = priv->q = NULL; priv->state = SLIP_RECV_NORMAL; priv->i = priv->recved = 0;
#if SLIP_RX_FROM_ISR
  priv->rxpackets = NULL;
#endif
  netif->state = priv;
  MIB2_INIT_NETIF(netif, snmp_ifType_slip, SLIP_SIO_SPEED(priv->sd));
#if SLIP_USE_RX_THREAD
  sys_thread_new(SLIPIF_THREAD_NAME, slipif_loop_thread, netif, SLIPIF_THREAD_STACKSIZE, SLIPIF_THREAD_PRIO);
#endif
  return ERR_OK;
}

void slipif_poll(struct netif *netif) { struct slipif_priv *priv = (struct slipif_priv *)netif->state; u8_t c; while (sio_tryread(priv->sd, &c, 1) > 0) slipif_rxbyte_input(netif, c); }

#if SLIP_RX_FROM_ISR
void slipif_process_rxqueue(struct netif *netif) {
  struct slipif_priv *priv = (struct slipif_priv *)netif->state; SYS_ARCH_DECL_PROTECT(old_level);
  SYS_ARCH_PROTECT(old_level);
  while (priv->rxpackets != NULL) { struct pbuf *p = priv->rxpackets;
#if SLIP_RX_QUEUE
    struct pbuf *q = p; while (q->len != q->tot_len && q->next) q = q->next; priv->rxpackets = q->next; q->next = NULL;
#else
    priv->rxpackets = NULL;
#endif
    SYS_ARCH_UNPROTECT(old_level); if (netif->input(p, netif) != ERR_OK) pbuf_free(p); SYS_ARCH_PROTECT(old_level); }
  SYS_ARCH_UNPROTECT(old_level);
}

static void slipif_rxbyte_enqueue(struct netif *netif, u8_t data) { struct pbuf *p; struct slipif_priv *priv = (struct slipif_priv *)netif->state; SYS_ARCH_DECL_PROTECT(old_level); p = slipif_rxbyte(netif, data); if (p) { SYS_ARCH_PROTECT(old_level); if (priv->rxpackets) {
#if SLIP_RX_QUEUE
  struct pbuf *q = p; while (q->next) q = q->next; q->next = p;
#else
  pbuf_free(priv->rxpackets);
#endif
  } else priv->rxpackets = p; SYS_ARCH_UNPROTECT(old_level); } }

void slipif_received_byte(struct netif *netif, u8_t data) { slipif_rxbyte_enqueue(netif, data); }
void slipif_received_bytes(struct netif *netif, u8_t *data, u8_t len) { u8_t i; for (i = 0; i < len; i++) slipif_rxbyte_enqueue(netif, data[i]); }
#endif

#endif /* LWIP_SLIP */