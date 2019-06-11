/* This file is part of SIS (SPARC/RISCV instruction simulator)

   Copyright (C) 2019 Free Software Foundation, Inc.
   Contributed by Jiri Gaisler, European Space Agency

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.  */

/* This code based on socket example at 
 * https://www.geeksforgeeks.org/socket-programming-cc/
 * and on sparc-stub.c from gdb.
 */

#include <unistd.h>
#include <stdio.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <stdlib.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <netdb.h>
#include <string.h>
#include <signal.h>
#include "sis.h"

#define EBREAK 0x00100073
#define CEBREAK 0x90002
int new_socket;
static char sendbuf[2048] = "$";
static const char hexchars[] = "0123456789abcdef";
static int detach = 0;

int
create_socket (int port)
{
  int server_fd;
  struct sockaddr_in address;
  int opt = 1;
  int addrlen = sizeof (address);
  struct protoent *proto;

  // Creating socket file descriptor 
  if ((server_fd = socket (AF_INET, SOCK_STREAM, 0)) == 0)
    {
      perror ("socket failed");
      return 0;
    }

  // Forcefully attaching socket to the port
  if (setsockopt (server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof (opt)))
    {
      perror ("setsockopt");
      return 0;
    }
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = INADDR_ANY;
  address.sin_port = htons (port);

  // Forcefully attaching socket to the port
  if (bind (server_fd, (struct sockaddr *) &address, sizeof (address)) < 0)
    {
      perror ("bind failed");
      return 0;
    }
  if (listen (server_fd, 1) < 0)
    {
      perror ("listen");
      return 0;
    }
  if ((new_socket = accept (server_fd, (struct sockaddr *) &address,
			    (socklen_t *) & addrlen)) < 0)
    {
      perror ("accept");
      return 0;
    }
  close (server_fd);
  setsockopt (new_socket, SOL_SOCKET, SO_KEEPALIVE, &opt, sizeof (opt));
  proto = getprotobyname ("tcp");
  setsockopt (new_socket, proto->p_proto, TCP_NODELAY, &opt, sizeof (opt));
  fcntl (new_socket, F_SETOWN, getpid ());
  fcntl (new_socket, F_SETFL, FASYNC);

  return 1;
}

static int
hex (unsigned char ch)
{
  if (ch >= 'a' && ch <= 'f')
    return ch - 'a' + 10;
  if (ch >= '0' && ch <= '9')
    return ch - '0';
  if (ch >= 'A' && ch <= 'F')
    return ch - 'A' + 10;
  return -1;
}

void
checksum (char *buf)
{
  unsigned char sum = 0;

  while (*buf)
    sum += *buf++;
  *buf++ = '#';
  *buf++ = hexchars[sum >> 4];
  *buf++ = hexchars[sum & 0x0f];
  *buf++ = 0;
}

int
check_pkg (unsigned char *buf, int len)
{
  int i, start = 0;
  unsigned char chksum = 0;
  unsigned char rxsum = 0;

  i = 0;
  while ((i < len) && (buf[i] != '$'))
    i++;
  if (i == len)
    return -1;
  i++;
  start = i;
  while ((i < len) && (buf[i] != '#'))
    {
      chksum = chksum + buf[i++];
    }
  if (i == len)
    return -1;
  i++;
  rxsum = (hex (buf[i]) << 4) | hex (buf[i + 1]);

  if ((i < len) && (chksum == rxsum))
    return start;
  return -1;
}

static void
int2hex (char *hexbuf, char *intbuf, int len)
{
  int i;
  for (i = 0; i < len; i++)
    {
      hexbuf[i * 2] = hexchars[(intbuf[i] >> 4) & 0x0f];
      hexbuf[i * 2 + 1] = hexchars[intbuf[i] & 0x0f];
    }
  hexbuf[len * 2] = 0;
}

static int
sim_stat ()
{
  int i;
  switch (simstat)
    {
    case NULL_HIT:
      i = SIGSEGV;
      break;
    case ERROR:
      i = SIGTERM;
      break;
    case CTRL_C:
      i = SIGINT;
      break;
    default:
      i = SIGTRAP;
    }
  return i;
}

int
gdb_remote_exec (char *buf)
{
  char membuf[1024];
  unsigned int i, j, len, addr;
  int cont = 1;
  char *cptr, *mptr;
  char *txbuf = &sendbuf[1];

  switch (buf[0])
    {
    case 'H':
      if (buf[1] != 'c')
	strcpy (txbuf, "OK");
      break;
    case '?':			/* last signal */
      if ((ebase.simtime == 0) && (sregs[cpu].pc == last_load_addr) &&
	  last_load_addr)
	sprintf (txbuf, "W%02d", sim_stat ());
      else
	sprintf (txbuf, "S%02d", sim_stat ());
      break;
    case 'D':			/* detach */
      strcpy (txbuf, "OK");
      detach = 1;
      break;
    case 'g':			/* get registers */
      len = arch->gdb_get_reg (membuf);
      int2hex (txbuf, membuf, len);
      break;
    case 'm':			/* read memory */
      i = 1;
      len = 0;
      addr = 0;
      while (buf[i] && (buf[i] != ','))
	{
	  addr = (addr << 4) | hex (buf[i]);
	  i++;
	}
      i++;
      while (buf[i] && (buf[i] != '#'))
	{
	  len = (len << 4) | hex (buf[i]);
	  i++;
	}
      sim_read (addr, membuf, len);
      int2hex (txbuf, membuf, len);
      break;
    case 'M':			/* write memory */
      i = 1;
      len = 0;
      addr = 0;
      while (buf[i] && (buf[i] != ','))
	{
	  addr = (addr << 4) | hex (buf[i]);
	  i++;
	}
      i++;
      while (buf[i] && (buf[i] != ':'))
	{
	  len = (len << 4) | hex (buf[i]);
	  i++;
	}
      i++;
      j = 0;
      while (buf[i] != '#')
	{
	  membuf[j] = (hex (buf[i]) << 4) | hex (buf[i + 1]);
	  i += 2;
	  j += 1;
	}
      sim_write (addr, membuf, len);
      strcpy (txbuf, "OK");
      break;
    case 'P':			/* write register */
      i = 1;
      len = 0;
      addr = 0;
      while (buf[i] && (buf[i] != '='))
	{
	  addr = (addr << 4) | hex (buf[i]);
	  i++;
	}
      i++;
      while (buf[i] && (buf[i] != '#'))
	{
	  if (cputype == CPU_RISCV)
	    {
	      j = hex (buf[i++]);
	      j <<= 4;
	      j |= hex (buf[i]);
	      len = (j << 24) | (len >> 8);	/* value is in target order! */
	    }
	  else
	    len = (len << 4) | hex (buf[i]);
	  i++;
	}
      i++;
      arch->set_register (&sregs[cpu], NULL, len, addr);
      strcpy (txbuf, "OK");
      break;
    case 'C':			/* continue execution */
      sim_create_inferior ();
    case 'c':
      sim_resume (0);
      i = sim_stat ();
      sprintf (txbuf, "S%02x", i);
      break;
    case 'k':			/* kill */
    case 'R':			/* restart */
      sim_create_inferior (0, 0, 0, 0);
      strcpy (txbuf, "OK");
      break;
    case 'v':
      if (strncmp (&buf[1], "Kill", 4) == 0)
	{			/* restart */
	  sim_create_inferior (0, 0, 0, 0);
	  strcpy (txbuf, "OK");
	}
      else if (strncmp (&buf[1], "Run;", 4) == 0)
	{			/* Restart */
	  sim_create_inferior (0, 0, 0, 0);
	  strcpy (txbuf, "S00");
	}
      else if (strncmp (&buf[1], "Cont", 4) == 0)
	{			/* continue/step */
	  switch (buf[5])
	    {
	    case '?':
	      strcpy (txbuf, "vCont;c;s");
	      break;
	    case 'c':
	      sim_resume (0);
	      i = sim_stat ();
	      sprintf (txbuf, "S%02x", i);
	      break;
	    case 's':
	      sim_resume (1);
	      i = sim_stat ();
	      sprintf (txbuf, "S%02x", i);
	      break;
	    default:
	      strcpy (sendbuf, "$#");
	    }
	}
      break;
    case 's':
    case 'S':
      sim_resume (1);
      i = sim_stat ();
      sprintf (txbuf, "S%02x", i);
      break;
    case 'z':
    case 'Z':
      if (buf[1] == '0')
	{			/* insert sw break */
	  i = 3;
	  addr = 0;
	  while (buf[i] && (buf[i] != ','))
	    {
	      addr = (addr << 4) | hex (buf[i]);
	      i++;
	    }
	  i++;
	  len = hex (buf[i]);
	  if (buf[0] == 'Z')
	    j = sim_insert_swbreakpoint (addr, len);
	  else
	    j = sim_remove_swbreakpoint (addr, len);
	  if (j)
	    strcpy (txbuf, "OK");
	  else
	    strcpy (txbuf, "E01");
	}
      else
	printf ("%s\n", buf);
      break;
    case 'q':			/* query */
      if (strncmp (&buf[1], "fThreadInfo", 11) == 0)
	{
	  strcpy (txbuf, "l");
	}
      else if (strncmp (&buf[1], "Attached", 8) == 0)
	{
	  strcpy (txbuf, "0");
	}
      else if (strncmp (&buf[1], "sThreadInfo", 11) == 0)
	{
	  strcpy (txbuf, "l");
	}
      else if (strncmp (&buf[1], "Rcmd", 4) == 0)
	{
	  cptr = &buf[6];
	  mptr = membuf;
	  while (*cptr != '#') {
	    *mptr = hex (*cptr++) << 4;
	    *mptr++ |= hex (*cptr++);
	  }
	  *mptr = 0;
	  exec_cmd (membuf);
	  strcpy (txbuf, "OK");
	}
      break;
    case '!':			/* extended protocl */
      strcpy (txbuf, "OK");
      break;
    default:
      printf ("%s\n", buf);
    }

  checksum (txbuf);
  return cont;
}

void
gdb_remote (int port)
{
  unsigned char buffer[2048];
  int cont = 1;
  int res, len = 0;
  char ack = '+';
  char nok = '-';

  sis_gdb_break = 1;
  detach = 0;
  signal (SIGIO, int_handler);
#ifdef __CYGWIN__
  printf ("Warning: gdb cannot interrupt a running simulator under CYGWIN\n");
  printf
    ("         As a workaround, use Ctrl-C in the simulator window instead\n\n");
#endif
  printf ("gdb: listening on port %d ", port);
  while (cont)
    {
      if ((cont = create_socket (port)))
	{
	  send (new_socket, &ack, 1, 0);
	  printf ("connected\n");
	}
      while (cont)
	{
	  do
	    {
	      len = read (new_socket, buffer, 2048);
	      buffer[len] = 0;
	      if (sis_verbose)
		printf ("%s (%d)\n", buffer, len);
	      if (len == 1)
		if (buffer[0] == '-')
		  {
		    if (sis_verbose)
		      printf ("tx: %s\n", sendbuf);
		    send (new_socket, sendbuf, strlen (sendbuf), 0);
		  }
		else if (buffer[0] == '+')
		  {
		    if (detach)
		      {
			cont = 0;
			break;
		      }
		  }
		else if (buffer[0] == 3)
		  {
		    ctrl_c = 1;
		  }
	      if (len <= 0)
		{
		  cont = 0;
		  break;
		}
	    }
	  while (len < 2);
	  if (!cont)
	    break;
	  res = check_pkg (buffer, len);
	  if (res > 0)
	    {
	      if (sis_verbose)
		printf ("tx: +\n");
	      send (new_socket, &ack, 1, 0);
	      if (detach)
		{
		  cont = 0;
		}
	      else
		{
		  strcpy (sendbuf, "$");
		  cont = gdb_remote_exec ((char *) &buffer[res]);
		  if (sis_verbose)
		    printf ("tx: %s\n", sendbuf);
		  send (new_socket, sendbuf, strlen (sendbuf), 0);
		}
	    }
	  else
	    {
	      if (sis_verbose)
		printf ("tx: -\n");
	      send (new_socket, &nok, 1, 0);
	    }
	}
    }
  if (new_socket)
    {
      close (new_socket);
    }
  new_socket = 0;
  sis_gdb_break = 0;
  signal (SIGIO, SIG_DFL);

}
