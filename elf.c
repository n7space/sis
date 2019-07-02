/* This file is part of SIS (SPARC/RISCV instruction simulator)

   Copyright (C) 2019 Free Software Foundation, Inc.
   Contributed by Jiri Gaisler, Sweden.

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

/* Very simple ELF program loader, only tested with RTEMS */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <netinet/in.h>
#include <elf.h>
#include <ctype.h>
#include "sis.h"

struct elf_file
{
  FILE *fp;
  Elf32_Ehdr ehdr;
  char *strtab;
  int arch;
  int cpu;
  int be;
};

static struct elf_file efile;

static int
read_elf_header (FILE * fp)
{
  Elf32_Ehdr ehdr;

  fseek (fp, 0, SEEK_SET);
  if (fread (&ehdr, sizeof (ehdr), 1, fp) != 1)
    {
      return (-1);
    }
  efile.fp = fp;

  if ((ehdr.e_ident[EI_MAG0] != 0x7f) ||
      strncmp ((char *) &ehdr.e_ident[EI_MAG1], "ELF", 3) != 0)
    {
      return (-1);
    }

  if (ehdr.e_ident[EI_DATA] == ELFDATA2MSB)
    {
      efile.be = 1;
      ehdr.e_entry = ntohl (ehdr.e_entry);
      ehdr.e_shoff = ntohl (ehdr.e_shoff);
      ehdr.e_phoff = ntohl (ehdr.e_phoff);
      ehdr.e_phnum = ntohs (ehdr.e_phnum);
      ehdr.e_shnum = ntohs (ehdr.e_shnum);
      ehdr.e_phentsize = ntohs (ehdr.e_phentsize);
      ehdr.e_shentsize = ntohs (ehdr.e_shentsize);
      ehdr.e_shstrndx = ntohs (ehdr.e_shstrndx);
      ehdr.e_machine = ntohs (ehdr.e_machine);
    }

  switch (ehdr.e_machine)
    {
    case EM_SPARC:
      if (sis_verbose)
	printf ("SPARC executable\n");
      efile.arch = CPU_ERC32;
      efile.cpu = CPU_ERC32;
      break;
    case EM_RISCV:
      if (sis_verbose)
	printf ("RISCV executable\n");
      efile.arch = CPU_RISCV;
      efile.cpu = CPU_RISCV;
      break;
    default:
      printf ("Unknown architecture (%d)\n", ehdr.e_machine);
      return (-1);
    }

  if (ehdr.e_ident[EI_CLASS] != 1)
    {
      printf ("Only 32-bit ELF supported!\n");
      return (-1);
    }
  efile.ehdr = ehdr;
  ebase.cpu = efile.cpu;
  return (ehdr.e_entry);
}

static int
read_elf_body ()
{
  Elf32_Ehdr ehdr = efile.ehdr;
  Elf32_Shdr sh, ssh;
  Elf32_Phdr ph[16];
  char *strtab;
  char *mem;
  uint32 *memw, i, j, k, vaddr;
  int be = efile.be;
  FILE *fp = efile.fp;

  fseek (fp, ehdr.e_shoff + ((ehdr.e_shstrndx) * ehdr.e_shentsize), SEEK_SET);
  if ((!fp) || (fread (&ssh, sizeof (ssh), 1, fp) != 1))
    {
      return (-1);
    }

  /* endian swap if big-endian target */
  if (be)
    {
      ssh.sh_name = ntohl (ssh.sh_name);
      ssh.sh_type = ntohl (ssh.sh_type);
      ssh.sh_offset = ntohl (ssh.sh_offset);
      ssh.sh_size = ntohl (ssh.sh_size);
    }
  strtab = (char *) malloc (ssh.sh_size);
  fseek (fp, ssh.sh_offset, SEEK_SET);
  if (fread (strtab, ssh.sh_size, 1, fp) != 1)
    {
      return (-1);
    }

  for (i = 0; i < ehdr.e_phnum; i++)
    {
      fseek (fp, ehdr.e_phoff + (i * ehdr.e_phentsize), SEEK_SET);
      if (fread (&ph[i], ehdr.e_phentsize, 1, fp) != 1)
	{
	  return (-1);
	}
      if (be)
	{
	  ph[i].p_type = ntohl (ph[i].p_type);
	  ph[i].p_offset = ntohl (ph[i].p_offset);
	  ph[i].p_vaddr = ntohl (ph[i].p_vaddr);
	  ph[i].p_paddr = ntohl (ph[i].p_paddr);
	  ph[i].p_filesz = ntohl (ph[i].p_filesz);
	  ph[i].p_memsz = ntohl (ph[i].p_memsz);
	}
    }

  for (i = 1; i < ehdr.e_shnum; i++)
    {
      fseek (fp, ehdr.e_shoff + (i * ehdr.e_shentsize), SEEK_SET);
      if (fread (&sh, sizeof (sh), 1, fp) != 1)
	{
	  return (-1);
	}
      if (be)
	{
	  sh.sh_name = ntohl (sh.sh_name);
	  sh.sh_addr = ntohl (sh.sh_addr);
	  sh.sh_size = ntohl (sh.sh_size);
	  sh.sh_type = ntohl (sh.sh_type);
	  sh.sh_offset = ntohl (sh.sh_offset);
	  sh.sh_flags = ntohl (sh.sh_flags);
	}

      if ((sh.sh_type != SHT_NOBITS) && (sh.sh_size)
	  && (sh.sh_flags & SHF_ALLOC))
	{
	  for (k = 0; k < ehdr.e_phnum; k++)
	    {
	      if ((sh.sh_addr >= ph[k].p_vaddr) &&
		  ((sh.sh_addr + sh.sh_size) <=
		   (ph[k].p_vaddr + ph[k].p_filesz)))
		{
		  vaddr = sh.sh_addr;
		  sh.sh_addr = sh.sh_addr - ph[k].p_vaddr + ph[k].p_paddr;
		  break;
		}
	    }
	  if (sis_verbose)
	    printf ("section: %s at 0x%x, size %d bytes\n",
		    &strtab[sh.sh_name], sh.sh_addr, sh.sh_size);
	  mem = calloc (sh.sh_size / 4 + 1, 4);
	  if (mem != NULL)
	    {
	      if ((sh.sh_type == SHT_PROGBITS)
		  || (sh.sh_type == SHT_INIT_ARRAY)
		  || (sh.sh_type == SHT_FINI_ARRAY))
		{
		  fseek (fp, sh.sh_offset, SEEK_SET);
		  if (fread (mem, sh.sh_size, 1, fp) != 1)
		    {
		      return (-1);
		    }
		  memw = (unsigned int *) mem;
		  if (be)
		    for (j = 0; j < (sh.sh_size) / 4 + 1; j++)
		      memw[j] = ntohl (memw[j]);
		  ms->sis_memory_write (sh.sh_addr, mem,
					(sh.sh_size / 4 + 1) * 4);
		}
	    }
	  else
	    {
	      return (-1);
	    }
	  free (mem);
	}
    }

  free (strtab);
  return (ehdr.e_entry);
}

int
elf_load (char *fname, int load)
{
  FILE *fp;
  int res;

  if ((fp = fopen (fname, "r")) == NULL)
    {
      printf ("file not found\n");
      return (-1);
    }


  res = read_elf_header (fp);
  if (res < 0)
    printf ("File read error\n");

  if (load && (res >= 0))
    {
      res = read_elf_body ();
      if (res < 0)
	printf ("File read error\n");
      else
	printf (" Loaded %s, entry 0x%08x\n", fname, res);
      fclose (efile.fp);
    }
  return res;

}
