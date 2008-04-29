/* sd2iec - SD/MMC to Commodore serial bus interface/controller
   Copyright (C) 2007,2008  Ingo Korb <ingo@akana.de>

   Inspiration and low-level SD/MMC access based on code from MMC2IEC
     by Lars Pontoppidan et al., see sdcard.c|h and config.h.

   FAT filesystem access based on code from ChaN and Jim Brain, see ff.c|h.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License only.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA


   diskchange.c: Disk image changer

*/

#include <avr/io.h>
#include <avr/pgmspace.h>
#include <string.h>
#include "config.h"
#include "buffers.h"
#include "doscmd.h"
#include "errormsg.h"
#include "fatops.h"
#include "flags.h"
#include "ff.h"
#include "parser.h"
#include "timer.h"
#include "ustring.h"
#include "diskchange.h"

static const char PROGMEM autoswap_name[] = "AUTOSWAP.LST";

static FIL     swaplist;
static path_t  swappath;
static uint8_t linenum;

static void mount_line(void) {
  FRESULT res;
  UINT bytesread;
  uint8_t i,*str,*strend;
  uint16_t curpos;

  /* Kill all buffers */
  free_all_user_buffers(1);

  curpos = 0;
  strend = NULL;

  for (i=0;i<=linenum;i++) {
    str = command_buffer;

    res = f_lseek(&swaplist,curpos);
    if (res != FR_OK) {
      parse_error(res,1);
      return;
    }

    res = f_read(&swaplist, str, CONFIG_COMMAND_BUFFER_SIZE, &bytesread);
    if (res != FR_OK) {
      parse_error(res,1);
      return;
    }

    /* Terminate string in buffer */
    if (bytesread < CONFIG_COMMAND_BUFFER_SIZE)
      str[bytesread] = 0;

    if (bytesread == 0) {
      /* End of file - restart loop to read the first entry */
      i = -1; /* I could've used goto instead... */
      linenum = 0;
      curpos = 0;
      continue;
    }

    /* Skip name */
    while (*str != '\r' && *str != '\n') str++;

    strend = str;

    /* Skip line terminator */
    while (*str == '\r' || *str == '\n') str++;

    curpos += str - command_buffer;
  }

  /* Terminate file name */
  *strend = 0;

  if (partition[swappath.part].fop != &fatops)
    image_unmount(swappath.part);

  /* Parse the path */
  path_t path;

  /* Start in the partition+directory of the swap list */
  current_part = swappath.part;
  partition[current_part].current_dir = swappath.fat;

  if (parse_path(command_buffer, &path, &str, 0))
    return;

  /* Mount the disk image */
  fat_chdir(&path, str);

  if (current_error != 0 && current_error != ERROR_DOSVERSION)
    return;

  /* Confirmation blink */
  for (i=0;i<2;i++) {
    tick_t targettime;

    DIRTY_LED_ON();
    BUSY_LED_ON();
    targettime = ticks + MS_TO_TICKS(100);
    while (time_before(ticks,targettime)) ;

    DIRTY_LED_OFF();
    BUSY_LED_OFF();
    targettime = ticks + MS_TO_TICKS(100);
    while (time_before(ticks,targettime)) ;
  }
}

void set_changelist(path_t *path, uint8_t *filename) {
  FRESULT res;

  /* Assume this isn't the auto-swap list */
  globalflags &= (uint8_t)~AUTOSWAP_ACTIVE;

  /* Remove the old swaplist */
  if (linenum != 255) {
    f_close(&swaplist);
    memset(&swaplist,0,sizeof(swaplist));
    linenum = 255;
  }

  if (ustrlen(filename) == 0)
    return;

  /* Open a new swaplist */
  partition[path->part].fatfs.curr_dir = path->fat;
  res = f_open(&partition[path->part].fatfs, &swaplist, filename, FA_READ | FA_OPEN_EXISTING);
  if (res != FR_OK) {
    parse_error(res,1);
    return;
  }

  /* Remember its directory so relative paths work */
  swappath = *path;

  linenum = 0;
  mount_line();
}


void change_disk(void) {
  path_t path;

  if (linenum == 255) {
    /* No swaplist active, try using AUTOSWAP.LST */
    /* change_disk is called from the IEC idle loop, so entrybuf is free */
    ustrcpy_P(entrybuf, autoswap_name);
    path.fat  = partition[current_part].current_dir;
    path.part = current_part;
    set_changelist(&path, entrybuf);
    if (linenum == 255) {
      /* No swap list found, clear error and exit */
      set_error(ERROR_OK);
      return;
    } else {
      /* Autoswaplist found, mark it as active                */
      /* and exit because the first image is already mounted. */
      globalflags |= AUTOSWAP_ACTIVE;
      return;
    }
  }

  /* Mount the next image in the list */
  linenum++;
  mount_line();
}

void init_change(void) {
  memset(&swaplist,0,sizeof(swaplist));
  linenum = 255;
  globalflags &= (uint8_t)~AUTOSWAP_ACTIVE;
}
