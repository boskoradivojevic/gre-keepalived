#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <arpa/inet.h>
#include <linux/ip.h>
#include <linux/udp.h>
#include <linux/if.h>
#include <linux/if_tunnel.h>
#include <linux/if_ether.h>
#include <linux/sockios.h>

#include "defines.h"
#include "structures.h"

#include "util.h"

t_tunnel *tunnels = NULL;

int c_tunnels = 0;
int work = 1;

int AddTunnel (char *name, in_addr_t local, in_addr_t remote, unsigned short ttl, int period, int retries, char *ifup, char *ifdown)
{
int i;

  for (i=0;i<c_tunnels;i++)
    if ((tunnels[i].local.s_addr == local) && (tunnels[i].remote.s_addr == remote))
      return 0;

  c_tunnels++;
  tunnels = ReallocateMemory (tunnels, (c_tunnels-1) * sizeof (t_tunnel), c_tunnels * sizeof (t_tunnel));

  tunnels[c_tunnels-1].local.s_addr = local;
  tunnels[c_tunnels-1].remote.s_addr = remote;
  tunnels[c_tunnels-1].ttl = ttl;
  tunnels[c_tunnels-1].retries = retries;
  tunnels[c_tunnels-1].period = period;
  tunnels[c_tunnels-1].keepalive_count = -1;
  tunnels[c_tunnels-1].transitions = 0;
  tunnels[c_tunnels-1].last_keepalive = 0;

  strncpy (tunnels[c_tunnels-1].name, name, MAX_NAME-1);
  strncpy (tunnels[c_tunnels-1].if_up, ifup, MAX_PATH-1);
  strncpy (tunnels[c_tunnels-1].if_down, ifdown, MAX_PATH-1);
  
  return 1;
}

int ParseConfigFile (char *name)
{
FILE *fp;
char line[CONFIG_LINE];
int num=0, i, valid=0;
char tmp_name[16], tmp_local[16], tmp_remote[16], tmp_ifup[256], tmp_ifdown[256];
int tmp_ttl, tmp_period, tmp_retries;

  fp = fopen (name, "r");
  if (!fp)
    return 0;
    
  while (fgets (line, CONFIG_LINE, fp) != NULL) {
    num++;
    if (line[0] != '#') {
      i = sscanf (line, "%s %s %s %d %d %d %s %s", tmp_name, tmp_local, tmp_remote, &tmp_ttl, &tmp_period, &tmp_retries, tmp_ifup, tmp_ifdown);
      if (!SanityCheckValues (i, tmp_name, tmp_local, tmp_remote, tmp_ttl, tmp_period, tmp_retries, tmp_ifup, tmp_ifdown)) {
        fprintf (stderr, "Error parsing config file, ignoring line %d!\n", num);
        continue;
      }
      valid++;
      AddTunnel (tmp_name, inet_addr (tmp_local), inet_addr (tmp_remote), tmp_ttl, tmp_period, tmp_retries, tmp_ifup, tmp_ifdown);
    }
  }
  fclose (fp);
  return valid;
}

int SetLinkStatus (int tunnel_id, int status)
{
__u32 mask = 0, flags = 0;
struct ifreq ifr;
int fd, err;
  
  if (status) { /* Interface should go UP! */
    mask |= IFF_UP;
    flags |= IFF_UP;
  } else { /* Interface should go DOWN! */
    mask |= IFF_UP;
    flags &= ~IFF_UP;
  }

  strncpy(ifr.ifr_name, tunnels[tunnel_id].name, IFNAMSIZ);
  fd = socket(PF_INET, SOCK_DGRAM, 0);
  if (fd < 0)
    return -1;

  err = ioctl(fd, SIOCGIFFLAGS, &ifr);
  if (err) {
    perror("SIOCGIFFLAGS");
    close(fd);
    return -1;
  }

  if ((ifr.ifr_flags^flags)&mask) {
    ifr.ifr_flags &= ~mask;
    ifr.ifr_flags |= mask&flags;
    err = ioctl(fd, SIOCSIFFLAGS, &ifr);
    if (err)
      perror("SIOCSIFFLAGS");
  }

  close(fd);
  return err;
}
                                                                                                                                        
int CreateTunnels ()
{
struct ip_tunnel_parm param;
int i, num=0;

  for (i=0;i<c_tunnels;i++) {
    FillTunnelParam (&param, tunnels[i].local.s_addr, tunnels[i].remote.s_addr, tunnels[i].ttl, tunnels[i].name);
    if (IoctlTunnel (&param, 1) == -1)
      printf ("Error adding tunnel %s\n", tunnels[i].name);
    num++;
  }
  return num;
}                            

int DestroyTunnels ()
{
struct ip_tunnel_parm param;
int i, num=0;

  for (i=0;i<c_tunnels;i++) {
    FillTunnelParam (&param, tunnels[i].local.s_addr, tunnels[i].remote.s_addr, tunnels[i].ttl, tunnels[i].name);
    if (IoctlTunnel (&param, 2) == -1)
      printf ("Error deleting tunnel %s\n", tunnels[i].name);
    num++;
  }
  return num;
}                            

int BindUDP ()
{
struct sockaddr_in sin;
int sockfd, sockopt=1;

  sockfd = socket (AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (sockfd == -1) {
    perror ("socket");
    return 0;
  }

  memset (&sin, 0, sizeof (sin));
  sin.sin_family = AF_INET;
  sin.sin_port = htons (UDP_PORT);
  sin.sin_addr.s_addr = htonl(INADDR_ANY);
  
  if (setsockopt (sockfd, IPPROTO_IP, IP_PKTINFO, &sockopt, sizeof (sockopt)) == -1) {
    perror ("setsockopt"); 
    return 0;
  }
  SetSocketNonBlock (sockfd);
  if (bind (sockfd, (struct sockaddr *) &sin, sizeof(struct sockaddr)) == -1) {
    perror ("bind");
    return 0;
  }
  
  return sockfd;
}

int InitSendingSocket () 
{
int sockfd, pad = 1;

  sockfd = socket (PF_INET, SOCK_RAW, IPPROTO_GRE);
  if (sockfd == -1) {
    perror ("socket");
    return 0;
  }

  if (setsockopt (sockfd, SOL_IP, IP_PKTINFO, (void *)&pad, sizeof(pad)) == -1) {
    perror ("setsockopt"); 
    return 0;
  }
  
  return sockfd;
}

int SendUDPKeepalive (int tunnel, int sockfd)
{
int err;
char datagram [32];
struct gre_header *real_greh = (struct gre_header *) datagram;
struct iphdr *inner_ip = (struct iphdr *) (datagram + 4);
struct udphdr *ka_udph = (struct udphdr *) (datagram + 24);
struct sockaddr_in sin, from;

struct iovec iov;
struct msghdr mesg;
struct cmsghdr *cmsg;
struct in_pktinfo *pkti;

char b[sizeof (struct cmsghdr) + sizeof (struct in_pktinfo)];

  sin.sin_family = AF_INET;
  sin.sin_addr.s_addr = tunnels[tunnel].remote.s_addr;

  from.sin_family = AF_INET;
  from.sin_addr.s_addr = tunnels[tunnel].local.s_addr;

  iov.iov_base = datagram;
  iov.iov_len = sizeof (datagram);
  mesg.msg_iov = &iov;
  mesg.msg_iovlen = 1;
  mesg.msg_name = &sin;
  mesg.msg_namelen = sizeof (sin);
  mesg.msg_control = b;
  mesg.msg_controllen = sizeof (b);
  mesg.msg_flags = 0;

  cmsg = CMSG_FIRSTHDR (&mesg);
  cmsg->cmsg_len = sizeof (b);
  cmsg->cmsg_level = SOL_IP;
  cmsg->cmsg_type = IP_PKTINFO;
  
  pkti = (struct in_pktinfo *)CMSG_DATA (cmsg);
  pkti->ipi_ifindex = 0;
  pkti->ipi_spec_dst = from.sin_addr;
  pkti->ipi_addr = from.sin_addr;
  
  memset (datagram, 0, 32);
  
  real_greh->flags = 0;
  real_greh->ptype = htons (ETH_P_IP); /* IP proto */

  inner_ip->ihl = sizeof(struct iphdr) >> 2;
  inner_ip->version = 4;
  inner_ip->protocol = IPPROTO_UDP;
  inner_ip->tos = 0;
  inner_ip->tot_len = htons(28);
  inner_ip->id = htonl (54321);
  inner_ip->frag_off = 0;
  inner_ip->ttl = 255;
  inner_ip->check = 0;
  inner_ip->saddr = tunnels[tunnel].remote.s_addr; /* Remote end */
  inner_ip->daddr = tunnels[tunnel].local.s_addr;  /* Our end */

  ka_udph->source = htons (54321);
  ka_udph->dest = htons (UDP_PORT);
  ka_udph->len = htons (8);
  ka_udph->check = 0;

  inner_ip->check = in_cksum ((const unsigned short *) (datagram+4), 20, 0);

  err = sendmsg (sockfd, &mesg, 0);
  return (err);
}

int LinkStatus (int tunnel_id)
{
__u32 mask = 0, flags = 0;
struct ifreq ifr;
int fd, err;
  
  mask |= IFF_UP;
  flags |= IFF_UP;

  strncpy(ifr.ifr_name, tunnels[tunnel_id].name, IFNAMSIZ);

  fd = socket(PF_INET, SOCK_DGRAM, 0);
  if (fd < 0)
    return -1;

  err = ioctl(fd, SIOCGIFFLAGS, &ifr);
  if (err) {
    perror("SIOCGIFFLAGS");
    close(fd);
    return -1;
  }

  if (!((ifr.ifr_flags^flags)&mask)) {
    close(fd);
    return 1;
  } else {
    close(fd);
    return 0;
  }
}

int CallScript (int tunnel_id, int which) // which == 1 IFUP, which == 2, IFDOWN
{
char script[MAX_PATH];
char local_ip[16], remote_ip[16];

  strncpy (local_ip, inet_ntoa (tunnels[tunnel_id].local), 15);
  strncpy (remote_ip, inet_ntoa (tunnels[tunnel_id].remote), 15);

  if (which == 1) 
    snprintf (script, MAX_PATH-1, "%s UP %s %s %s", tunnels[tunnel_id].if_up, tunnels[tunnel_id].name, local_ip, remote_ip);

  if (which == 2) 
    snprintf (script, MAX_PATH-1, "%s DOWN %s %s %s", tunnels[tunnel_id].if_down, tunnels[tunnel_id].name, local_ip, remote_ip);

  if (script[0] && script[0] != '-')
    return system (script);

  return 0;
}

int SendKeepalives (int sockfd)
{
time_t now = time (NULL);
int i, valid=0;

  for (i=0;i<c_tunnels;i++) {
    if ((tunnels[i].last_keepalive + tunnels[i].period) <= now) {
      if (SendUDPKeepalive (i, sockfd) != -1) {
        tunnels[i].last_keepalive = now;
        valid++;
        if (tunnels[i].keepalive_count == -1)
          tunnels[i].keepalive_count = 1;
        else
          tunnels[i].keepalive_count++;
        if (tunnels[i].keepalive_count > tunnels[i].retries) {
          if (LinkStatus (i)) {
            SetLinkStatus (i, 0);
            tunnels[i].transitions++;
            CallScript (i, 2);
          }
        }
      }
    }
  }
  return valid;
}

int FindTunnelID (in_addr_t local, in_addr_t remote)
{
int i;

  for (i=0;i<c_tunnels;i++)
    if ((tunnels[i].local.s_addr == local) && (tunnels[i].remote.s_addr == remote))
      return i;

  return -1;
}


int AckKeepalive (struct in_addr remote_s, struct in_addr local_s)
{
in_addr_t remote = remote_s.s_addr;
in_addr_t local = local_s.s_addr;
int tunnelid;

  tunnelid = FindTunnelID (local, remote);

  tunnels[tunnelid].keepalive_count = 0;
  if (!LinkStatus (tunnelid)) {
    SetLinkStatus (tunnelid, 1);
    tunnels[tunnelid].transitions++;
    CallScript (tunnelid, 1); 
  }

  return 1;
}

void DumpStatistics ()
{
int i;
FILE *fp;

  fp = fopen (STAT_FILE, "w");
  if (!fp)
    return;
    
  for (i=0;i<c_tunnels;i++) {
    fprintf (fp, "Tunnel_id: %d, Name: %s, Transitions: %ld, Keepalive_count: %ld\n", i, tunnels[i].name,
      tunnels[i].transitions, tunnels[i].keepalive_count);
  }
  fclose (fp);
}

static void sig_handler (int sig)
{
  switch (sig) {
    case SIGINT:
    case SIGTERM:
    case SIGKILL:
    case SIGQUIT:
      work = 0;
      DestroyTunnels ();
      break;
    case SIGUSR1:
      DumpStatistics ();
      break;
  }
}

int ReceiveMSG (int sockfd)
{
struct sockaddr_in cliaddr;
struct msghdr msg;
union control_data cmsg;
struct cmsghdr *cmsgptr;
struct iovec iov[1];
char buf[128];
int num=0;

  iov[0].iov_base = buf;
  iov[0].iov_len = sizeof (buf);
  memset (&msg, 0, sizeof (msg));
  msg.msg_name = &cliaddr;
  msg.msg_namelen = sizeof (cliaddr);
  msg.msg_iov = iov;
  msg.msg_iovlen = 1;
  msg.msg_control = &cmsg;
  msg.msg_controllen = sizeof (cmsg);
  
  while ((recvmsg (sockfd, &msg, 0) != -1) && (work == 1)) {
    num ++;
    for (cmsgptr = CMSG_FIRSTHDR (&msg); cmsgptr != NULL; cmsgptr = CMSG_NXTHDR(&msg,cmsgptr)) {
      if (cmsgptr->cmsg_level == IPPROTO_IP && cmsgptr->cmsg_type == IP_PKTINFO) {
        AckKeepalive (cliaddr.sin_addr, *(struct in_addr *)dstaddr (cmsgptr));
      }  
    }
  }

  return num;
}

int main (int argc, char *argv[])
{
char config[MAX_PATH];
int tmp, sockfd, send_sockfd;

  if (argc > 1)
    strncpy (config, argv[1], MAX_PATH-1);
  else
    strncpy (config, DEFAULT_CONFIG, MAX_PATH-1);
  
  if (!FileExists (config)) {
    fprintf (stderr, "Fatal error! Config file %s does not exist! Exiting...\n", config);
    exit (-1);
  }
  if (!ParseConfigFile (config)) {
    fprintf (stderr, "Fatal error! No valid tunnels created! Exiting...\n");
    exit (-1);
  }

  printf ("Config: %d tunnels configured.\n", c_tunnels);
  tmp = CreateTunnels();
  if (tmp)
    printf ("Init: %d tunnels created.\n", tmp);
  else {
    fprintf (stderr, "Fatal error: No tunnels created! Exiting...\n");
    exit (-1);
  }
  
  sockfd = BindUDP ();
  if (!sockfd) {
    fprintf (stderr, "Fatal error: Unable to bind UDP port %d!\n", UDP_PORT);
    exit (-1);
  }

  send_sockfd = InitSendingSocket();
  if (!sockfd) {
    fprintf (stderr, "Fatal error: Unable to bind!\n");
    exit (-1);
  }

  signal (SIGINT, sig_handler);
  signal (SIGTERM, sig_handler);
  signal (SIGQUIT, sig_handler);
  signal (SIGKILL, sig_handler);
  signal (SIGUSR1, sig_handler);

  daemonize ();

  while (work) {
    SendKeepalives (send_sockfd);
    sleep (1);
    ReceiveMSG (sockfd);
  }

// Let's clean up
  free (tunnels);
  close (sockfd);
  close (send_sockfd);

  return 0;
}
