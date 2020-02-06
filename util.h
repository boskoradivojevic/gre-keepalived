void *ReallocateMemory (void *, size_t, size_t);
int SanityCheckValues (int, char *, char *, char *, unsigned short, int, int, char *, char *);
int FillTunnelParam (struct ip_tunnel_parm *, in_addr_t, in_addr_t, unsigned short, char *);
int IoctlTunnel (void *, int);
void SetSocketNonBlock (int);
unsigned short in_cksum(const u_short *, register u_int, int);
void daemonize (void);
int FileExists (char *);
