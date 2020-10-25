/* Based on Documentation/networking/tuntap.txt */

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include "sis.h"
#ifdef __linux__
#include <sys/socket.h>
#include <sys/file.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <linux/if_tun.h>
#include <linux/if_bridge.h>
#include <net/if_arp.h>		/* for ARPHRD_ETHER */
#include <arpa/inet.h>
#include <poll.h>

#define POLLTIME 5000

static struct ifreq ifr;
static int fd, err, sockfd;
static char dev[64];
static int tun_fd, nread, br_socket_fd;
static int br_add_interface (const char *bridge, const char *dev);
static void sis_tap_poll ();

int
sis_tap_init (long unsigned emac)
{
  int i;
  long unsigned xmac, xmac2;
  if (fd)
    return 1;
  if ((fd = open ("/dev/net/tun", O_RDWR)) < 0)
    {
      printf ("Cannot open TUN/TAP dev\n");
      return 0;
    }

  memset (&ifr, 0, sizeof (ifr));

  /* Flags: IFF_TUN   - TUN device (no Ethernet headers)
   *        IFF_TAP   - TAP device
   *
   *        IFF_NO_PI - Do not provide packet information
   */

  ifr.ifr_flags = IFF_TAP | IFF_NO_PI;

  if ((err = ioctl (fd, TUNSETIFF, (void *) &ifr)) < 0)
    {
      printf ("ERR: Could not ioctl tun: %s\n", strerror (errno));
      close (fd);
      return 0;
    }

  strcpy (dev, ifr.ifr_name);

  sockfd = socket (PF_PACKET, SOCK_RAW, htons (ETH_P_ALL));

  ifr.ifr_flags = IFF_UP;
  if ((err = ioctl (sockfd, SIOCSIFFLAGS, (void *) &ifr)) < 0)
    {
      printf ("net: could not enable tun %s\n", strerror (errno));
      close (fd);
      close (sockfd);
      return 0;
    }

  ifr.ifr_hwaddr.sa_family = ARPHRD_ETHER;

  xmac = 0;
  for (i = 0; i < 6; i++)
    {
      xmac <<= 8;
      xmac |= (emac >> (i * 8)) & 0x0ff;
    }

  xmac2 = xmac;

  memcpy (ifr.ifr_hwaddr.sa_data, &xmac, 6);
  if (ioctl (sockfd, SIOCSIFHWADDR, &ifr) < 0)
    {
      printf ("net: hwaddr error\n");
      close (fd);
      close (sockfd);
      return 0;
    }

  if (ioctl (sockfd, SIOCGIFHWADDR, &ifr) < 0)
    {
      printf ("net: hwaddr error\n");
      close (fd);
      close (sockfd);
      return 0;
    }
  memcpy (&xmac, ifr.ifr_hwaddr.sa_data, 6);

  if (xmac != xmac2)
    {
      printf ("net: could not set hwaddr\n");
      close (fd);
      close (sockfd);
      return 0;
    }

  printf ("net: using %s, ether %lx", dev, emac);
  if (bridge[0])
    {
      if (br_add_interface (bridge, dev))
	{
	  printf ("ERR: Could not attach %s to bridge %s: %s\n",
		  dev, bridge, strerror (errno));
	  /*
	     close (fd);
	     close (sockfd);
	     return 0;
	   */
	}
      else
	printf (", bridge %s\n", bridge);
    }
  printf ("\n");
  tun_fd = fd;
  event (sis_tap_poll, 0, POLLTIME);
  return 1;
}

int
sis_tap_write (unsigned char *buffer, int len)
{
  int i, nwrite;
  nwrite = write (tun_fd, buffer, len);
  if (nread < 0)
    {
      perror ("Writing from interface");
      close (tun_fd);
      exit (1);
    }
  if (sis_verbose)
    {
      printf ("Write %d bytes to device %s\n", len, dev);
      for (i = 0; i < len; i++)
	printf ("%02x", buffer[i]);
      printf ("\n");
    }
}

static void
sis_tap_poll ()
{
  struct pollfd fds[1];
  int ret;
  unsigned char buffer[1536];

  fds[0].fd = tun_fd;
  fds[0].events = POLLRDNORM;
  ret = poll (fds, 1, 0);

  if (ret > 0)
    {
      if (fds[0].revents & POLLRDNORM)
	{
	  nread = read (tun_fd, buffer, 1536);
	  if (nread < 0)
	    {
	      perror ("Reading from interface");
	      close (tun_fd);
	      exit (1);
	    }
	  greth_rxready (buffer, nread);
	}
    }
  event (sis_tap_poll, 0, POLLTIME);
}

/* Attach tap device to bridge */
/* Taken from bridge-utils-1.6 */
static int
br_add_interface (const char *bridge, const char *dev)
{
  struct ifreq ifr;
  int err;
  int ifindex = if_nametoindex (dev);

  if ((br_socket_fd = socket (AF_LOCAL, SOCK_STREAM, 0)) < 0)
    return ENODEV;
  if (ifindex == 0)
    return ENODEV;

  strncpy (ifr.ifr_name, bridge, IFNAMSIZ);
#ifdef SIOCBRADDIF
  ifr.ifr_ifindex = ifindex;
  err = ioctl (br_socket_fd, SIOCBRADDIF, &ifr);
#endif
  {
    unsigned long args[4] = { BRCTL_ADD_IF, ifindex, 0, 0 };

    ifr.ifr_data = (char *) args;
    err = ioctl (br_socket_fd, SIOCDEVPRIVATE, &ifr);
  }

  return err < 0 ? errno : 0;
}

#else

int
sis_tap_init (long unsigned emac)
{
  printf ("Error: networking not supported on this host\n");
}

int
sis_tap_write (unsigned char *buffer, int len)
{
  printf ("Error: networking not supported on this host\n");
}
#endif
