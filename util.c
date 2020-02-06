#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/stat.h>
#include <arpa/inet.h>
#include <linux/ip.h>
#include <linux/udp.h>
#include <linux/if.h>
#include <linux/if_tunnel.h>
#include <linux/if_ether.h>
#include <linux/sockios.h>

#include "util.h"

void *ReallocateMemory (void *src, size_t old_size, size_t new_size)
{
void *ret;

  ret = malloc (new_size);
  if (src) {
    memcpy (ret, src, old_size);
    free (src);
  }
  return ret;
}

int SanityCheckValues (int num, char *name, char *local, char *remote, unsigned short ttl, int period, int retries, char *ifup, char *ifdown)
{
  if (num < 6)
    return 0;
  
  if ((inet_addr (local) == -1) || (inet_addr (remote) == -1))
    return 0;
  
  if (ttl > 255)
    return 0;

  if (!period || !retries)
    return 0;
    
  return 1;
}

int FillTunnelParam (struct ip_tunnel_parm *p, in_addr_t local, in_addr_t remote, unsigned short ttl, char *name)
{
  memset(p, 0, sizeof(*p));

  p->iph.version = 4;
  p->iph.ihl = 5;
  p->iph.frag_off = htons(0x4000); /* Don't fragment */
  p->iph.protocol = IPPROTO_GRE;
  p->iph.daddr = remote;
  p->iph.saddr = local;
  p->iph.ttl = ttl;
  strncpy(p->name, name, IFNAMSIZ);
  
  return 1;
}

int IoctlTunnel (void *p, int act) // act==1 ADD, act==2 DEL
{
struct ifreq ifr;
int fd, err;

  strncpy(ifr.ifr_name, "gre0", IFNAMSIZ);
  ifr.ifr_ifru.ifru_data = p;

  fd = socket(PF_NETLINK, SOCK_DGRAM, 0);

  if (act == 1)
    err = ioctl(fd, SIOCADDTUNNEL, &ifr);
  if (act == 2)
    err = ioctl(fd, SIOCDELTUNNEL, &ifr);

  if (err)
    perror("ioctl");
  close(fd);
  return err;
}

void SetSocketNonBlock (int sock)
{
int flags;

  flags = fcntl (sock, F_GETFL, 0);
  fcntl (sock, F_SETFL, flags | O_NONBLOCK);
}

unsigned short in_cksum(const u_short *addr, register u_int len, int csum)
{
	int nleft = len;
	const u_short *w = addr;
	u_short answer;
	int sum = csum;

	while (nleft > 1)  {
		sum += *w++;
		nleft -= 2;
	}
	if (nleft == 1)
		sum += htons(*(u_char *)w<<8);

	sum = (sum >> 16) + (sum & 0xffff);
	sum += (sum >> 16);
	answer = ~sum;
	return (answer);
}

void daemonize (void)
{
pid_t pid, sid;

  if (getppid() == 1)
    return;
    
  pid = fork();
  if (pid < 0) /* Bad PID, error! */
    exit (1);
  
  if (pid > 0) { /* We are parent process, die... :) */
    exit (0);
  }

  umask (0);
  
  sid = setsid ();
  if (sid < 0)
    exit (1);
  
  freopen ("/dev/null", "r", stdin);
  freopen ("/dev/console", "w", stdout);
  freopen ("/dev/console", "w", stderr);
}

int FileExists (char *name)
{
FILE *fp;

  fp = fopen (name, "r");
  if (!fp)
    return 0;
  fclose (fp);  
  return 1;
}                               
