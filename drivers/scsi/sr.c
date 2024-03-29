/*
 *      sr.c Copyright (C) 1992 David Giller
 *	     Copyright (C) 1993, 1994 Eric Youngdale
 *
 *      adapted from:
 *	sd.c Copyright (C) 1992 Drew Eckhardt 
 *	Linux scsi disk driver by
 *		Drew Eckhardt 
 *
 *	<drew@colorado.edu>
 *
 *       Modified by Eric Youngdale ericy@cais.com to
 *       add scatter-gather, multiple outstanding request, and other
 *       enhancements.
 */

#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/cdrom.h>
#include <asm/system.h>

#define MAJOR_NR SCSI_CDROM_MAJOR
#include "../block/blk.h"
#include "scsi.h"
#include "hosts.h"
#include "sr.h"
#include "scsi_ioctl.h"   /* For the door lock/unlock commands */
#include "constants.h"

#define MAX_RETRIES 3
#define SR_TIMEOUT 5000

static void sr_init(void);
static void sr_finish(void);
static void sr_attach(Scsi_Device *);
static int sr_detect(Scsi_Device *);

struct Scsi_Device_Template sr_template = {NULL, "cdrom", "sr", TYPE_ROM, 
					     SCSI_CDROM_MAJOR, 0, 0, 0, 1,
					     sr_detect, sr_init,
					     sr_finish, sr_attach, NULL};

Scsi_CD * scsi_CDs;
static int * sr_sizes;

static int * sr_blocksizes;

static int sr_open(struct inode *, struct file *);
static void get_sectorsize(int);

extern int sr_ioctl(struct inode *, struct file *, unsigned int, unsigned long);

void requeue_sr_request (Scsi_Cmnd * SCpnt);
static int check_cdrom_media_change(dev_t);

static void sr_release(struct inode * inode, struct file * file)
{
	sync_dev(inode->i_rdev);
	if(! --scsi_CDs[MINOR(inode->i_rdev)].device->access_count)
	  sr_ioctl(inode, NULL, SCSI_IOCTL_DOORUNLOCK, 0);
}

static struct file_operations sr_fops = 
{
	NULL,			/* lseek - default */
	block_read,		/* read - general block-dev read */
	block_write,		/* write - general block-dev write */
	NULL,			/* readdir - bad */
	NULL,			/* select */
	sr_ioctl,		/* ioctl */
	NULL,			/* mmap */
	sr_open,       		/* special open code */
	sr_release,		/* release */
	NULL,			/* fsync */
	NULL,			/* fasync */
	check_cdrom_media_change,  /* Disk change */
	NULL			/* revalidate */
};

/*
 * This function checks to see if the media has been changed in the
 * CDROM drive.  It is possible that we have already sensed a change,
 * or the drive may have sensed one and not yet reported it.  We must
 * be ready for either case. This function always reports the current
 * value of the changed bit.  If flag is 0, then the changed bit is reset.
 * This function could be done as an ioctl, but we would need to have
 * an inode for that to work, and we do not always have one.
 */

int check_cdrom_media_change(dev_t full_dev){
	int retval, target;
	struct inode inode;
	int flag = 0;

	target =  MINOR(full_dev);

	if (target >= sr_template.nr_dev) {
		printk("CD-ROM request error: invalid device.\n");
		return 0;
	};

	inode.i_rdev = full_dev;  /* This is all we really need here */
	retval = sr_ioctl(&inode, NULL, SCSI_IOCTL_TEST_UNIT_READY, 0);

	if(retval){ /* Unable to test, unit probably not ready.  This usually
		     means there is no disc in the drive.  Mark as changed,
		     and we will figure it out later once the drive is
		     available again.  */

	  scsi_CDs[target].device->changed = 1;
	  return 1; /* This will force a flush, if called from
		       check_disk_change */
	};

	retval = scsi_CDs[target].device->changed;
	if(!flag) {
	  scsi_CDs[target].device->changed = 0;
	  /* If the disk changed, the capacity will now be different,
	     so we force a re-read of this information */
	  if (retval) scsi_CDs[target].needs_sector_size = 1;
	};
	return retval;
}

/*
 * rw_intr is the interrupt routine for the device driver.  It will be notified on the 
 * end of a SCSI read / write, and will take on of several actions based on success or failure.
 */

static void rw_intr (Scsi_Cmnd * SCpnt)
{
	int result = SCpnt->result;
	int this_count = SCpnt->this_count;

#ifdef DEBUG
	printk("sr.c done: %x %x\n",result, SCpnt->request.bh->b_data);
#endif
	if (!result)
		{ /* No error */
		  if (SCpnt->use_sg == 0) {
		    if (SCpnt->buffer != SCpnt->request.buffer)
		      {
			int offset;
			offset = (SCpnt->request.sector % 4) << 9;
			memcpy((char *)SCpnt->request.buffer, 
			       (char *)SCpnt->buffer + offset, 
			       this_count << 9);
			/* Even though we are not using scatter-gather, we look
			   ahead and see if there is a linked request for the
			   other half of this buffer.  If there is, then satisfy
			   it. */
			if((offset == 0) && this_count == 2 &&
			   SCpnt->request.nr_sectors > this_count && 
			   SCpnt->request.bh &&
			   SCpnt->request.bh->b_reqnext &&
			   SCpnt->request.bh->b_reqnext->b_size == 1024) {
			  memcpy((char *)SCpnt->request.bh->b_reqnext->b_data, 
				 (char *)SCpnt->buffer + 1024, 
				 1024);
			  this_count += 2;
			};
			
			scsi_free(SCpnt->buffer, 2048);
		      }
		  } else {
		    struct scatterlist * sgpnt;
		    int i;
		    sgpnt = (struct scatterlist *) SCpnt->buffer;
		    for(i=0; i<SCpnt->use_sg; i++) {
		      if (sgpnt[i].alt_address) {
			if (sgpnt[i].alt_address != sgpnt[i].address) {
			  memcpy(sgpnt[i].alt_address, sgpnt[i].address, sgpnt[i].length);
			};
			scsi_free(sgpnt[i].address, sgpnt[i].length);
		      };
		    };
		    scsi_free(SCpnt->buffer, SCpnt->sglist_len);  /* Free list of scatter-gather pointers */
		    if(SCpnt->request.sector % 4) this_count -= 2;
/* See   if there is a padding record at the end that needs to be removed */
		    if(this_count > SCpnt->request.nr_sectors)
		      this_count -= 2;
		  };

#ifdef DEBUG
		printk("(%x %x %x) ",SCpnt->request.bh, SCpnt->request.nr_sectors, 
		       this_count);
#endif
		if (SCpnt->request.nr_sectors > this_count)
			{	 
			SCpnt->request.errors = 0;
			if (!SCpnt->request.bh)
			    panic("sr.c: linked page request (%lx %x)",
				  SCpnt->request.sector, this_count);
			}

		  SCpnt = end_scsi_request(SCpnt, 1, this_count);  /* All done */
		  requeue_sr_request(SCpnt);
		  return;
		} /* Normal completion */

	/* We only come through here if we have an error of some kind */

/* Free up any indirection buffers we allocated for DMA purposes. */
	if (SCpnt->use_sg) {
	  struct scatterlist * sgpnt;
	  int i;
	  sgpnt = (struct scatterlist *) SCpnt->buffer;
	  for(i=0; i<SCpnt->use_sg; i++) {
	    if (sgpnt[i].alt_address) {
	      scsi_free(sgpnt[i].address, sgpnt[i].length);
	    };
	  };
	  scsi_free(SCpnt->buffer, SCpnt->sglist_len);  /* Free list of scatter-gather pointers */
	} else {
	  if (SCpnt->buffer != SCpnt->request.buffer)
	    scsi_free(SCpnt->buffer, SCpnt->bufflen);
	};

	if (driver_byte(result) != 0) {
		if ((SCpnt->sense_buffer[0] & 0x7f) == 0x70) {
			if ((SCpnt->sense_buffer[2] & 0xf) == UNIT_ATTENTION) {
				/* detected disc change.  set a bit and quietly refuse	*/
				/* further access.					*/
		    
				scsi_CDs[DEVICE_NR(SCpnt->request.dev)].device->changed = 1;
				SCpnt = end_scsi_request(SCpnt, 0, this_count);
			        requeue_sr_request(SCpnt);
				return;
			}
		}
	    
		if (SCpnt->sense_buffer[2] == ILLEGAL_REQUEST) {
			printk("CD-ROM error: Drive reports ILLEGAL REQUEST.\n");
			if (scsi_CDs[DEVICE_NR(SCpnt->request.dev)].ten) {
				scsi_CDs[DEVICE_NR(SCpnt->request.dev)].ten = 0;
				requeue_sr_request(SCpnt);
				result = 0;
				return;
			} else {
			  printk("CD-ROM error: Drive reports %d.\n", SCpnt->sense_buffer[2]);				
			  SCpnt = end_scsi_request(SCpnt, 0, this_count);
			  requeue_sr_request(SCpnt); /* Do next request */
			  return;
			}

		}

		if (SCpnt->sense_buffer[2] == NOT_READY) {
			printk("CDROM not ready.  Make sure you have a disc in the drive.\n");
			SCpnt = end_scsi_request(SCpnt, 0, this_count);
			requeue_sr_request(SCpnt); /* Do next request */
			return;
		};
	      }
	
	/* We only get this far if we have an error we have not recognized */
	if(result) {
	  printk("SCSI CD error : host %d id %d lun %d return code = %03x\n", 
		 scsi_CDs[DEVICE_NR(SCpnt->request.dev)].device->host->host_no, 
		 scsi_CDs[DEVICE_NR(SCpnt->request.dev)].device->id,
		 scsi_CDs[DEVICE_NR(SCpnt->request.dev)].device->lun,
		 result);
	    
	  if (status_byte(result) == CHECK_CONDITION)
		  print_sense("sr", SCpnt);
	  
	  SCpnt = end_scsi_request(SCpnt, 0, SCpnt->request.current_nr_sectors);
	  requeue_sr_request(SCpnt);
  }
}

/*
 * Here I tried to implement better support for PhotoCD's.
 * 
 * Much of this has do be done with vendor-specific SCSI-commands.
 * So I have to complete it step by step. Useful information is welcome.
 *
 * Actually works:
 *   - NEC:     Detection and support of multisession CD's. Special handling
 *              for XA-disks is not necessary.
 *     
 *   - TOSHIBA: setting density is done here now, mounting PhotoCD's should
 *              work now without running the program "set_density"
 *              People reported that it is necessary to eject and reinsert
 *              the CD after the set-density call to get this working for
 *              old drives.
 *              And some very new drives don't need this call any more...
 *              Multisession CD's are supported too.
 *
 * Dec 1994: completely rewritten, uses kernel_scsi_ioctl() now
 *
 *   kraxel@cs.tu-berlin.de (Gerd Knorr)
 */

static void sr_photocd(struct inode *inode)
{
  unsigned long   sector,min,sec,frame;
  unsigned char   buf[40];
  int             rc;

  if (!suser()) {
    /* I'm not the superuser, so SCSI_IOCTL_SEND_COMMAND isn't allowed for me.
     * That's why mpcd_sector will be initialized with zero, because I'm not
     * able to get the right value. Necessary only if access_count is 1, else
     * no disk change happened since the last call of this function and we can
     * keep the old value.
     */
    if (1 == scsi_CDs[MINOR(inode->i_rdev)].device->access_count)
      scsi_CDs[MINOR(inode->i_rdev)].mpcd_sector = 0;
    return;
  }
  
  switch(scsi_CDs[MINOR(inode->i_rdev)].device->manufacturer) {

  case SCSI_MAN_NEC:
#ifdef DEBUG
    printk("sr_photocd: use NEC code\n");
#endif
    memset(buf,0,40);
    *((unsigned long*)buf)   = 0;
    *((unsigned long*)buf+1) = 0x16;
    buf[8+0] = 0xde;
    buf[8+1] = 0x03;
    buf[8+2] = 0xb0;
    rc = kernel_scsi_ioctl(scsi_CDs[MINOR(inode->i_rdev)].device,
			   SCSI_IOCTL_SEND_COMMAND, buf);
    if (rc != 0) {
      printk("sr_photocd: ioctl error (NEC): 0x%x\n",rc);
      sector = 0;
    } else {
      min   = (unsigned long)buf[8+15]/16*10 + (unsigned long)buf[8+15]%16;
      sec   = (unsigned long)buf[8+16]/16*10 + (unsigned long)buf[8+16]%16;
      frame = (unsigned long)buf[8+17]/16*10 + (unsigned long)buf[8+17]%16;
      sector = min*60*75 + sec*75 + frame;
#ifdef DEBUG
      if (sector) {
	printk("sr_photocd: multisession CD detected. start: %lu\n",sector);
      }
#endif
    }
    break;

  case SCSI_MAN_TOSHIBA:
#ifdef DEBUG
    printk("sr_photocd: use TOSHIBA code\n");
#endif
    
    /* first I do a set_density-call (for reading XA-sectors) ... */
    memset(buf,0,40);
    *((unsigned long*)buf)   = 12;
    *((unsigned long*)buf+1) = 12;
    buf[8+0] = 0x15;
    buf[8+1] = (1 << 4);
    buf[8+2] = 12;
    buf[14+ 3] = 0x08;
    buf[14+ 4] = 0x83;
    buf[14+10] = 0x08;
    rc = kernel_scsi_ioctl(scsi_CDs[MINOR(inode->i_rdev)].device,
			   SCSI_IOCTL_SEND_COMMAND, buf);
    if (rc != 0) {
      printk("sr_photocd: ioctl error (TOSHIBA #1): 0x%x\n",rc);
    }

    /* ... and then I ask, if there is a multisession-Disk */
    memset(buf,0,40);
    *((unsigned long*)buf)   = 0;
    *((unsigned long*)buf+1) = 4;
    buf[8+0] = 0xc7;
    buf[8+1] = 3;
    rc = kernel_scsi_ioctl(scsi_CDs[MINOR(inode->i_rdev)].device,
			   SCSI_IOCTL_SEND_COMMAND, buf);
    if (rc != 0) {
      printk("sr_photocd: ioctl error (TOSHIBA #2): 0x%x\n",rc);
      sector = 0;
    } else {
      min   = (unsigned long)buf[8+1]/16*10 + (unsigned long)buf[8+1]%16;
      sec   = (unsigned long)buf[8+2]/16*10 + (unsigned long)buf[8+2]%16;
      frame = (unsigned long)buf[8+3]/16*10 + (unsigned long)buf[8+3]%16;
      sector = min*60*75 + sec*75 + frame;
      if (sector) {
        sector -= CD_BLOCK_OFFSET;
#ifdef DEBUG
        printk("sr_photocd: multisession CD detected: start: %lu\n",sector);
#endif
      }
    }
    break;

  case SCSI_MAN_UNKNOWN:
  default:
#ifdef DEBUG
    printk("sr_photocd: unknown drive, no special multisession code\n");
#endif
    sector = 0;
    break; }

  scsi_CDs[MINOR(inode->i_rdev)].mpcd_sector = sector;
  return;
}

static int sr_open(struct inode * inode, struct file * filp)
{
	if(MINOR(inode->i_rdev) >= sr_template.nr_dev || 
	   !scsi_CDs[MINOR(inode->i_rdev)].device) return -ENXIO;   /* No such device */

	if (filp->f_mode & 2)  
	    return -EROFS;

        check_disk_change(inode->i_rdev);

	if(!scsi_CDs[MINOR(inode->i_rdev)].device->access_count++)
	  sr_ioctl(inode, NULL, SCSI_IOCTL_DOORLOCK, 0);

	/* If this device did not have media in the drive at boot time, then
	   we would have been unable to get the sector size.  Check to see if
	   this is the case, and try again.
	   */

	if(scsi_CDs[MINOR(inode->i_rdev)].needs_sector_size)
	  get_sectorsize(MINOR(inode->i_rdev));

#if 1	/* don't use for now - it doesn't seem to work for everybody */
	sr_photocd(inode);
#endif

	return 0;
}


/*
 * do_sr_request() is the request handler function for the sr driver.  Its function in life 
 * is to take block device requests, and translate them to SCSI commands.
 */
	
static void do_sr_request (void)
{
  Scsi_Cmnd * SCpnt = NULL;
  struct request * req = NULL;
  unsigned long flags;
  int flag = 0;

  while (1==1){
    save_flags(flags);
    cli();
    if (CURRENT != NULL && CURRENT->dev == -1) {
      restore_flags(flags);
      return;
    };
    
    INIT_SCSI_REQUEST;

    if (flag++ == 0)
      SCpnt = allocate_device(&CURRENT,
			      scsi_CDs[DEVICE_NR(MINOR(CURRENT->dev))].device, 0); 
    else SCpnt = NULL;
    restore_flags(flags);

/* This is a performance enhancement.  We dig down into the request list and
   try and find a queueable request (i.e. device not busy, and host able to
   accept another command.  If we find one, then we queue it. This can
   make a big difference on systems with more than one disk drive.  We want
   to have the interrupts off when monkeying with the request list, because
   otherwise the kernel might try and slip in a request in between somewhere. */

    if (!SCpnt && sr_template.nr_dev > 1){
      struct request *req1;
      req1 = NULL;
      save_flags(flags);
      cli();
      req = CURRENT;
      while(req){
	SCpnt = request_queueable(req,
				  scsi_CDs[DEVICE_NR(MINOR(req->dev))].device);
	if(SCpnt) break;
	req1 = req;
	req = req->next;
      };
      if (SCpnt && req->dev == -1) {
	if (req == CURRENT) 
	  CURRENT = CURRENT->next;
	else
	  req1->next = req->next;
      };
      restore_flags(flags);
    };
    
    if (!SCpnt)
      return; /* Could not find anything to do */
    
  wake_up(&wait_for_request);

/* Queue command */
  requeue_sr_request(SCpnt);
  };  /* While */
}    

void requeue_sr_request (Scsi_Cmnd * SCpnt)
{
	unsigned int dev, block, realcount;
	unsigned char cmd[10], *buffer, tries;
	int this_count, start, end_rec;

	tries = 2;

      repeat:
	if(!SCpnt || SCpnt->request.dev <= 0) {
	  do_sr_request();
	  return;
	}

	dev =  MINOR(SCpnt->request.dev);
	block = SCpnt->request.sector;	
	buffer = NULL;
	this_count = 0;

	if (dev >= sr_template.nr_dev)
		{
		/* printk("CD-ROM request error: invalid device.\n");			*/
		SCpnt = end_scsi_request(SCpnt, 0, SCpnt->request.nr_sectors);
		tries = 2;
		goto repeat;
		}

	if (!scsi_CDs[dev].use)
		{
		/* printk("CD-ROM request error: device marked not in use.\n");		*/
		SCpnt = end_scsi_request(SCpnt, 0, SCpnt->request.nr_sectors);
		tries = 2;
		goto repeat;
		}

	if (scsi_CDs[dev].device->changed)
	        {
/* 
 * quietly refuse to do anything to a changed disc until the changed bit has been reset
 */
		/* printk("CD-ROM has been changed.  Prohibiting further I/O.\n");	*/
		SCpnt = end_scsi_request(SCpnt, 0, SCpnt->request.nr_sectors);
		tries = 2;
		goto repeat;
		}
	
	switch (SCpnt->request.cmd)
		{
		case WRITE: 		
			SCpnt = end_scsi_request(SCpnt, 0, SCpnt->request.nr_sectors);
			goto repeat;
			break;
		case READ : 
		        cmd[0] = READ_6;
			break;
		default : 
			panic ("Unknown sr command %d\n", SCpnt->request.cmd);
		}
	
	cmd[1] = (SCpnt->lun << 5) & 0xe0;

/*
           Now do the grungy work of figuring out which sectors we need, and
	   where in memory we are going to put them.

	   The variables we need are:

	   this_count= number of 512 byte sectors being read 
	   block     = starting cdrom sector to read.
	   realcount = # of cdrom sectors to read

	   The major difference between a scsi disk and a scsi cdrom
is that we will always use scatter-gather if we can, because we can
work around the fact that the buffer cache has a block size of 1024,
and we have 2048 byte sectors.  This code should work for buffers that
are any multiple of 512 bytes long.  */

#if 1
	/* Here we redirect the volume descriptor block of the CD-ROM.
	 * Necessary for multisession CD's, until the isofs-routines
	 * handle this via the CDROMMULTISESSION_SYS call
	 */
	if (block >= 64 && block < 68) {
	  block += scsi_CDs[dev].mpcd_sector*4; }
#endif
	
	SCpnt->use_sg = 0;

	if (SCpnt->host->sg_tablesize > 0 &&
	    (!need_isa_buffer ||
	    dma_free_sectors >= 10)) {
	  struct buffer_head * bh;
	  struct scatterlist * sgpnt;
	  int count, this_count_max;
	  bh = SCpnt->request.bh;
	  this_count = 0;
	  count = 0;
	  this_count_max = (scsi_CDs[dev].ten ? 0xffff : 0xff) << 4;
	  /* Calculate how many links we can use.  First see if we need
	   a padding record at the start */
	  this_count = SCpnt->request.sector % 4;
	  if(this_count) count++;
	  while(bh && count < SCpnt->host->sg_tablesize) {
	    if ((this_count + (bh->b_size >> 9)) > this_count_max) break;
	    this_count += (bh->b_size >> 9);
	    count++;
	    bh = bh->b_reqnext;
	  };
	  /* Fix up in case of an odd record at the end */
	  end_rec = 0;
	  if(this_count % 4) {
	    if (count < SCpnt->host->sg_tablesize) {
	      count++;
	      end_rec = (4 - (this_count % 4)) << 9;
	      this_count += 4 - (this_count % 4);
	    } else {
	      count--;
	      this_count -= (this_count % 4);
	    };
	  };
	  SCpnt->use_sg = count;  /* Number of chains */
	  count = 512;/* scsi_malloc can only allocate in chunks of 512 bytes*/
	  while( count < (SCpnt->use_sg * sizeof(struct scatterlist))) 
	    count = count << 1;
	  SCpnt->sglist_len = count;
	  sgpnt = (struct scatterlist * ) scsi_malloc(count);
	  if (!sgpnt) {
	    printk("Warning - running *really* short on DMA buffers\n");
	    SCpnt->use_sg = 0;  /* No memory left - bail out */
	  } else {
	    buffer = (unsigned char *) sgpnt;
	    count = 0;
	    bh = SCpnt->request.bh;
	    if(SCpnt->request.sector % 4) {
	      sgpnt[count].length = (SCpnt->request.sector % 4) << 9;
	      sgpnt[count].address = (char *) scsi_malloc(sgpnt[count].length);
	      if(!sgpnt[count].address) panic("SCSI DMA pool exhausted.");
	      sgpnt[count].alt_address = sgpnt[count].address; /* Flag to delete
								  if needed */
	      count++;
	    };
	    for(bh = SCpnt->request.bh; count < SCpnt->use_sg; 
		count++, bh = bh->b_reqnext) {
	      if (bh) { /* Need a placeholder at the end of the record? */
		sgpnt[count].address = bh->b_data;
		sgpnt[count].length = bh->b_size;
		sgpnt[count].alt_address = NULL;
	      } else {
		sgpnt[count].address = (char *) scsi_malloc(end_rec);
		if(!sgpnt[count].address) panic("SCSI DMA pool exhausted.");
		sgpnt[count].length = end_rec;
		sgpnt[count].alt_address = sgpnt[count].address;
		if (count+1 != SCpnt->use_sg) panic("Bad sr request list");
		break;
	      };
	      if (((int) sgpnt[count].address) + sgpnt[count].length > 
		  ISA_DMA_THRESHOLD & (SCpnt->host->unchecked_isa_dma)) {
		sgpnt[count].alt_address = sgpnt[count].address;
		/* We try and avoid exhausting the DMA pool, since it is easier
		   to control usage here.  In other places we might have a more
		   pressing need, and we would be screwed if we ran out */
		if(dma_free_sectors < (sgpnt[count].length >> 9) + 5) {
		  sgpnt[count].address = NULL;
		} else {
		  sgpnt[count].address = (char *) scsi_malloc(sgpnt[count].length);
		};
/* If we start running low on DMA buffers, we abort the scatter-gather
   operation, and free all of the memory we have allocated.  We want to
   ensure that all scsi operations are able to do at least a non-scatter/gather
   operation */
		if(sgpnt[count].address == NULL){ /* Out of dma memory */
		  printk("Warning: Running low on SCSI DMA buffers");
		  /* Try switching back to a non scatter-gather operation. */
		  while(--count >= 0){
		    if(sgpnt[count].alt_address) 
		      scsi_free(sgpnt[count].address, sgpnt[count].length);
		  };
		  SCpnt->use_sg = 0;
		  scsi_free(buffer, SCpnt->sglist_len);
		  break;
		}; /* if address == NULL */
	      };  /* if need DMA fixup */
	    };  /* for loop to fill list */
#ifdef DEBUG
	    printk("SG: %d %d %d %d %d *** ",SCpnt->use_sg, SCpnt->request.sector,
		   this_count, 
		   SCpnt->request.current_nr_sectors,
		   SCpnt->request.nr_sectors);
	    for(count=0; count<SCpnt->use_sg; count++)
	      printk("SGlist: %d %x %x %x\n", count,
		     sgpnt[count].address, 
		     sgpnt[count].alt_address, 
		     sgpnt[count].length);
#endif
	  };  /* Able to allocate scatter-gather list */
	};
	
	if (SCpnt->use_sg == 0){
	  /* We cannot use scatter-gather.  Do this the old fashion way */
	  if (!SCpnt->request.bh)  	
	    this_count = SCpnt->request.nr_sectors;
	  else
	    this_count = (SCpnt->request.bh->b_size >> 9);
	  
	  start = block % 4;
	  if (start)
	    {				  
	      this_count = ((this_count > 4 - start) ? 
			    (4 - start) : (this_count));
	      buffer = (unsigned char *) scsi_malloc(2048);
	    } 
	  else if (this_count < 4)
	    {
	      buffer = (unsigned char *) scsi_malloc(2048);
	    }
	  else
	    {
	      this_count -= this_count % 4;
	      buffer = (unsigned char *) SCpnt->request.buffer;
	      if (((int) buffer) + (this_count << 9) > ISA_DMA_THRESHOLD & 
		  (SCpnt->host->unchecked_isa_dma))
		buffer = (unsigned char *) scsi_malloc(this_count << 9);
	    }
	};

	if (scsi_CDs[dev].sector_size == 2048)
	  block = block >> 2; /* These are the sectors that the cdrom uses */
	else
	  block = block & 0xfffffffc;

	realcount = (this_count + 3) / 4;

	if (scsi_CDs[dev].sector_size == 512) realcount = realcount << 2;

	if (((realcount > 0xff) || (block > 0x1fffff)) && scsi_CDs[dev].ten) 
		{
		if (realcount > 0xffff)
		        {
			realcount = 0xffff;
			this_count = realcount * (scsi_CDs[dev].sector_size >> 9);
			}

		cmd[0] += READ_10 - READ_6 ;
		cmd[2] = (unsigned char) (block >> 24) & 0xff;
		cmd[3] = (unsigned char) (block >> 16) & 0xff;
		cmd[4] = (unsigned char) (block >> 8) & 0xff;
		cmd[5] = (unsigned char) block & 0xff;
		cmd[6] = cmd[9] = 0;
		cmd[7] = (unsigned char) (realcount >> 8) & 0xff;
		cmd[8] = (unsigned char) realcount & 0xff;
		}
	else
		{
		  if (realcount > 0xff)
		    {
		      realcount = 0xff;
		      this_count = realcount * (scsi_CDs[dev].sector_size >> 9);
		    }
		  
		  cmd[1] |= (unsigned char) ((block >> 16) & 0x1f);
		  cmd[2] = (unsigned char) ((block >> 8) & 0xff);
		  cmd[3] = (unsigned char) block & 0xff;
		  cmd[4] = (unsigned char) realcount;
		  cmd[5] = 0;
		}   

#ifdef DEBUG
	{ 
	  int i;
	  printk("ReadCD: %d %d %d %d\n",block, realcount, buffer, this_count);
	  printk("Use sg: %d\n", SCpnt->use_sg);
	  printk("Dumping command: ");
	  for(i=0; i<12; i++) printk("%2.2x ", cmd[i]);
	  printk("\n");
	};
#endif

/* Some dumb host adapters can speed transfers by knowing the
 * minimum transfersize in advance.
 *
 * We shouldn't disconnect in the middle of a sector, but the cdrom
 * sector size can be larger than the size of a buffer and the
 * transfer may be split to the size of a buffer.  So it's safe to
 * assume that we can at least transfer the minimum of the buffer
 * size (1024) and the sector size between each connect / disconnect.
 */

        SCpnt->transfersize = (scsi_CDs[dev].sector_size > 1024) ?
                        1024 : scsi_CDs[dev].sector_size;

	SCpnt->this_count = this_count;
	scsi_do_cmd (SCpnt, (void *) cmd, buffer, 
		     realcount * scsi_CDs[dev].sector_size, 
		     rw_intr, SR_TIMEOUT, MAX_RETRIES);
}

static int sr_detect(Scsi_Device * SDp){
  
  /* We do not support attaching loadable devices yet. */
  if(scsi_loadable_module_flag) return 0;
  if(SDp->type != TYPE_ROM && SDp->type != TYPE_WORM) return 0;

  printk("Detected scsi CD-ROM sr%d at scsi%d, id %d, lun %d\n", 
	 sr_template.dev_noticed++,
	 SDp->host->host_no , SDp->id, SDp->lun); 

	 return 1;
}

static void sr_attach(Scsi_Device * SDp){
  Scsi_CD * cpnt;
  int i;
  
  /* We do not support attaching loadable devices yet. */
  
  if(scsi_loadable_module_flag) return;
  if(SDp->type != TYPE_ROM && SDp->type != TYPE_WORM) return;
  
  if (sr_template.nr_dev >= sr_template.dev_max)
    panic ("scsi_devices corrupt (sr)");
  
  for(cpnt = scsi_CDs, i=0; i<sr_template.dev_max; i++, cpnt++) 
    if(!cpnt->device) break;
  
  if(i >= sr_template.dev_max) panic ("scsi_devices corrupt (sr)");
  
  SDp->scsi_request_fn = do_sr_request;
  scsi_CDs[i].device = SDp;
  sr_template.nr_dev++;
  if(sr_template.nr_dev > sr_template.dev_max)
    panic ("scsi_devices corrupt (sr)");
}
     

static void sr_init_done (Scsi_Cmnd * SCpnt)
{
  struct request * req;
  
  req = &SCpnt->request;
  req->dev = 0xfffe; /* Busy, but indicate request done */
  
  if (req->sem != NULL) {
    up(req->sem);
  }
}

static void get_sectorsize(int i){
  unsigned char cmd[10];
  unsigned char *buffer;
  int the_result, retries;
  Scsi_Cmnd * SCpnt;
  
  buffer = (unsigned char *) scsi_malloc(512);
  SCpnt = allocate_device(NULL, scsi_CDs[i].device, 1);

  retries = 3;
  do {
    cmd[0] = READ_CAPACITY;
    cmd[1] = (scsi_CDs[i].device->lun << 5) & 0xe0;
    memset ((void *) &cmd[2], 0, 8);
    SCpnt->request.dev = 0xffff;  /* Mark as really busy */
    SCpnt->cmd_len = 0;
    
    memset(buffer, 0, 8);

    scsi_do_cmd (SCpnt,
		 (void *) cmd, (void *) buffer,
		 512, sr_init_done,  SR_TIMEOUT,
		 MAX_RETRIES);
    
    if (current == task[0])
      while(SCpnt->request.dev != 0xfffe);
    else
      if (SCpnt->request.dev != 0xfffe){
      	struct semaphore sem = MUTEX_LOCKED;
	SCpnt->request.sem = &sem;
	down(&sem);
	/* Hmm.. Have to ask about this */
	while (SCpnt->request.dev != 0xfffe) schedule();
      };
    
    the_result = SCpnt->result;
    retries--;
    
  } while(the_result && retries);
  
  SCpnt->request.dev = -1;  /* Mark as not busy */
  
  wake_up(&SCpnt->device->device_wait); 

  if (the_result) {
    scsi_CDs[i].capacity = 0x1fffff;
    scsi_CDs[i].sector_size = 2048;  /* A guess, just in case */
    scsi_CDs[i].needs_sector_size = 1;
  } else {
    scsi_CDs[i].capacity = (buffer[0] << 24) |
      (buffer[1] << 16) | (buffer[2] << 8) | buffer[3];
    scsi_CDs[i].sector_size = (buffer[4] << 24) |
      (buffer[5] << 16) | (buffer[6] << 8) | buffer[7];
    if(scsi_CDs[i].sector_size == 0) scsi_CDs[i].sector_size = 2048;
    if(scsi_CDs[i].sector_size != 2048 && 
       scsi_CDs[i].sector_size != 512) {
      printk ("scd%d : unsupported sector size %d.\n",
	      i, scsi_CDs[i].sector_size);
      scsi_CDs[i].capacity = 0;
      scsi_CDs[i].needs_sector_size = 1;
    };
    if(scsi_CDs[i].sector_size == 2048)
      scsi_CDs[i].capacity *= 4;
    scsi_CDs[i].needs_sector_size = 0;
  };
  scsi_free(buffer, 512);
}

static void sr_init()
{
	int i;
	static int sr_registered = 0;

	if(sr_template.dev_noticed == 0) return;

	if(!sr_registered) {
	  if (register_blkdev(MAJOR_NR,"sr",&sr_fops)) {
	    printk("Unable to get major %d for SCSI-CD\n",MAJOR_NR);
	    return;
	  }
	}

	/* We do not support attaching loadable devices yet. */
	if(scsi_loadable_module_flag) return;

	sr_template.dev_max = sr_template.dev_noticed;
	scsi_CDs = (Scsi_CD *) scsi_init_malloc(sr_template.dev_max * sizeof(Scsi_CD));
	memset(scsi_CDs, 0, sr_template.dev_max * sizeof(Scsi_CD));

	sr_sizes = (int *) scsi_init_malloc(sr_template.dev_max * sizeof(int));
	memset(sr_sizes, 0, sr_template.dev_max * sizeof(int));

	sr_blocksizes = (int *) scsi_init_malloc(sr_template.dev_max * 
						 sizeof(int));
	for(i=0;i<sr_template.dev_max;i++) sr_blocksizes[i] = 2048;
	blksize_size[MAJOR_NR] = sr_blocksizes;

}

void sr_finish()
{
  int i;

	for (i = 0; i < sr_template.nr_dev; ++i)
		{
		  get_sectorsize(i);
		  printk("Scd sectorsize = %d bytes\n", scsi_CDs[i].sector_size);
		  scsi_CDs[i].use = 1;
		  scsi_CDs[i].ten = 1;
		  scsi_CDs[i].remap = 1;
		  sr_sizes[i] = scsi_CDs[i].capacity;
		}

	blk_dev[MAJOR_NR].request_fn = DEVICE_REQUEST;
	blk_size[MAJOR_NR] = sr_sizes;	

	/* If our host adapter is capable of scatter-gather, then we increase
	   the read-ahead to 16 blocks (32 sectors).  If not, we use
	   a two block (4 sector) read ahead. */
	if(scsi_CDs[0].device->host->sg_tablesize)
	  read_ahead[MAJOR_NR] = 32;  /* 32 sector read-ahead.  Always removable. */
	else
	  read_ahead[MAJOR_NR] = 4;  /* 4 sector read-ahead */

	return;
}	
