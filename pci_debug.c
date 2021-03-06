/* pci_debug.c
 *
 * 6/21/2010 D. W. Hawkins
 *
 * PCI debug registers interface.
 *
 * This tool provides a debug interface for reading and writing
 * to PCI registers via the device base address registers (BARs).
 * The tool uses the PCI resource nodes automatically created
 * by recently Linux kernels.
 *
 * The readline library is used for the command line interface
 * so that up-arrow command recall works. Command-line history
 * is not implemented. Use -lreadline -lcurses when building.
 *
 * ----------------------------------------------------------------
 */
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <unistd.h>
#include <byteswap.h>
#include <pciaccess.h>


/* Readline support */
#include <readline/readline.h>
#include <readline/history.h>

/* PCI device */
typedef struct {
	/* Base address region */
	unsigned int bar;

	/* Slot info */
	unsigned int domain;
	unsigned int bus;
	unsigned int slot;
	unsigned int function;

	/* Resource filename */
	char         filename[100];

	/* File descriptor of the resource */
	int          fd;

	/* Memory mapped resource */
	unsigned char *maddr;
	unsigned int   size;
	unsigned int   offset;

	/* PCI physical address */
	unsigned int   phys;

	/* Address to pass to read/write (includes offset) */
	unsigned char *addr;
} device_t;

typedef struct {
	uint32_t Stop : 1;
	uint32_t INT : 1;
	uint32_t LLP : 1;
	uint32_t LIE : 1;
	uint32_t RIE : 1;
	uint32_t RSVD : 26;
	uint32_t OWN : 1;
} desc_ctrl_t;
typedef struct {
	uint32_t DAR_Low;
	uint32_t DAR_High;
	uint32_t SAR_Low;
	uint32_t SAR_High;
	uint32_t Transfer_Size;
	desc_ctrl_t desc_ctrl;
} desc_info;

void display_help(device_t *dev);
void parse_command(device_t *dev);
int process_command(device_t *dev, char *cmd);
int change_mem(device_t *dev, char *cmd);
int fill_mem(device_t *dev, char *cmd);
int display_mem(device_t *dev, char *cmd);
int change_endian(device_t *dev, char *cmd);
void pcie_mem_enable(void);

/* Endian read/write mode */
static int big_endian = 0;

/* Low-level access functions */
static void
write_8(
	device_t     *dev,
	unsigned int  addr,
	unsigned char data);

static unsigned char
read_8(
	device_t    *dev,
	unsigned int addr);

static void
write_le16(
	device_t          *dev,
	unsigned int       addr,
	unsigned short int data);

static unsigned short int
read_le16(
	device_t    *dev,
	unsigned int addr);

static void
write_be16(
	device_t          *dev,
	unsigned int       addr,
	unsigned short int data);

static unsigned short int
read_be16(
	device_t    *dev,
	unsigned int addr);

static void
write_le32(
	device_t    *dev,
	unsigned int addr,
	unsigned int data);

static unsigned int
read_le32(
	device_t    *dev,
	unsigned int addr);

static void
write_be32(
	device_t    *dev,
	unsigned int addr,
	unsigned int data);

static unsigned int
read_be32(
	device_t    *dev,
	unsigned int addr);

/* Usage */
static void show_usage()
{
	printf("\nUsage: pci_debug -s <device>\n"\
		 "  -h            Help (this message)\n"\
		 "  -s <device>   Slot/device (as per lspci)\n" \
		 "  -b <BAR>      Base address region (BAR) to access, eg. 0 for BAR0\n\n");
}
void mem_disp(void *mem_addr, uint32_t data_size)
{
    uint32_t columns, rows;
    uint32_t column_cnt, row_cnt;
    uint32_t *ptr = NULL;
    column_cnt = 8;
    row_cnt = data_size / column_cnt / 4;
    if (column_cnt == 16)
        printf("<--addr---dword-->: 03----00 07----04 11----08 15----12 19----16 23----20 27----24 31----28 "
                 "35----32 39----36 43----40 47----44 51----48 55----52 59----56 63----60\n");
    else
        printf("<--addr---dword-->: 03----00 07----04 11----08 15----12 19----16 23----20 27----24 31----28\n");

    ptr = (uint32_t *)mem_addr;
    for (rows = 0; rows < row_cnt; rows++)
    {
        // LOG_INFO("%p: ", mem_addr + rows * 4 * column_cnt);
        printf("0x%016lx: ", (uint64_t)(mem_addr + rows * 4 * column_cnt));
        // LOG_INFO("0x%-16lx: ", (qword_t)(+ rows * 4 * column_cnt));
        for (columns = 0; columns < column_cnt; columns++)
        {
            printf("%08x ", ptr[columns]);
        }
        printf("\n");
        ptr += column_cnt;
    }
}
void *boot_buffer;
uint32_t ep_addr = 0x0603d000;
// uint32_t ep_addr = 0x20000000;
uint32_t rc_addr = 0;
uint32_t desc_size = 0;
uint32_t desc_data_size = 4;
desc_info desc[40] = {0};
uint32_t data_cnt = 0;
unsigned long phys_addr;
int main(int argc, char *argv[])
{
	int opt;
	char *slot = 0;
	int status;
	struct stat statbuf;
	device_t device;
	device_t *dev = &device;	
	int fd = 0;
	char attr[1024];
	uint32_t cnt = 0;
	// system("setpci -s 1:0.0 4.b=6");
	// system("setpci -s 1:0.0 5.b=0");
	pcie_mem_enable();
	/* Clear desc mem*/
if ((fd = open("/dev/udmabuf0", O_RDWR)) != -1)
    {
        boot_buffer = mmap(NULL, 0x100000, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        /* Do some read/write access to buf */
        //mem_disp((void *)boot_buffer, 256);f
        close(fd);
    }
    if ((fd = open("/sys/class/u-dma-buf/udmabuf0/phys_addr", O_RDONLY)) != -1)
    {
        read(fd, attr, 1024);
        sscanf(attr, "%lx", &phys_addr);	
		printf("phys_addr:0x%lx\n", phys_addr);
        close(fd);
    }
	for(cnt = 0; cnt < 0x1900; cnt += 0x4) {
		*(volatile uint32_t*)(boot_buffer + sizeof(desc) + cnt) = cnt;
	}
	// for(cnt = 0; cnt < 0x900; cnt += 0x4) {
	// 	*(volatile uint32_t*)(boot_buffer + 0x900 + sizeof(desc) + cnt) = cnt;
	// }

	memset((void *)(boot_buffer), 0x0, sizeof(desc));

	for(desc_size = 0; desc_size < 20; desc_size += 2)
	{		
		desc[desc_size].SAR_High = phys_addr+(sizeof(desc[0]) * (desc_size+1) );
		desc[desc_size].desc_ctrl.LLP = 1;

		desc[desc_size+1].DAR_Low = ep_addr + (data_cnt * desc_data_size); 
		// desc[0].DAR_Low = phys_addr + sizeof(desc);
		desc[desc_size+1].DAR_High = 0x0;
		desc[desc_size+1].SAR_Low = phys_addr + sizeof(desc) + (data_cnt++ * desc_data_size);
		// desc[0].SAR_Low = 0x0603d000;
		desc[desc_size+1].SAR_High = 0x0;
		desc[desc_size+1].Transfer_Size = desc_data_size;
		desc[desc_size+1].desc_ctrl.Stop = 0;
		desc[desc_size+1].desc_ctrl.INT = 0;
		desc[desc_size+1].desc_ctrl.LIE = 1;
		desc[desc_size+1].desc_ctrl.RIE = 0;
		desc[desc_size+1].desc_ctrl.LLP = 0;
		desc[desc_size+1].desc_ctrl.OWN = 1;		
	}
	desc[19].desc_ctrl.INT = 1;
	desc[19].desc_ctrl.Stop = 1;

	for(desc_size = 20, data_cnt = 0; desc_size < 40; desc_size += 2)
	{		
		desc[desc_size].SAR_High = phys_addr+(sizeof(desc[0]) * (desc_size+1) );
		desc[desc_size].desc_ctrl.LLP = 1;

		// desc[desc_size+1].DAR_Low = ep_addr + (desc_size * (desc_data_size - 1)); 
		desc[desc_size+1].DAR_Low = (phys_addr + 0x2000) + (data_cnt * desc_data_size);
		desc[desc_size+1].DAR_High = 0x0;
		// desc[desc_size+1].SAR_Low = phys_addr + sizeof(desc) + (desc_size * (desc_data_size - 1));
		desc[desc_size+1].SAR_Low = ep_addr + (data_cnt++ * desc_data_size);
		desc[desc_size+1].SAR_High = 0x0;
		desc[desc_size+1].Transfer_Size = desc_data_size;
		desc[desc_size+1].desc_ctrl.Stop = 0;
		desc[desc_size+1].desc_ctrl.INT = 0;
		desc[desc_size+1].desc_ctrl.LIE = 1;
		desc[desc_size+1].desc_ctrl.RIE = 0;
		desc[desc_size+1].desc_ctrl.LLP = 0;
		desc[desc_size+1].desc_ctrl.OWN = 1;		
	}
	desc[39].desc_ctrl.INT = 1;
	desc[39].desc_ctrl.Stop = 1;

	memcpy((void *)(boot_buffer), desc, sizeof(desc));
	mem_disp((void *)(boot_buffer), 0x1000);

	/* Clear the structure fields */
	memset(dev, 0, sizeof(device_t));

	while ((opt = getopt(argc, argv, "b:hs:")) != -1) {
		switch (opt) {
			case 'b':
				/* Defaults to BAR0 if not provided */
				dev->bar = atoi(optarg);
				break;
			case 'h':
				show_usage();
				return -1;
			case 's':
				slot = optarg;
				break;
			default:
				show_usage();
				return -1;
		}
	}
	if (slot == 0) {
		show_usage();
		return -1;
	}

	/* ------------------------------------------------------------
	 * Open and map the PCI region
	 * ------------------------------------------------------------
	 */

	/* Extract the PCI parameters from the slot string */
	status = sscanf(slot, "%2x:%2x.%1x",
			&dev->bus, &dev->slot, &dev->function);
	if (status != 3) {
		printf("Error parsing slot information!\n");
		show_usage();
		return -1;
	}

	/* Convert to a sysfs resource filename and open the resource */
	snprintf(dev->filename, 99, "/sys/bus/pci/devices/%04x:%02x:%02x.%1x/resource%d",
			dev->domain, dev->bus, dev->slot, dev->function, dev->bar);
	dev->fd = open(dev->filename, O_RDWR | O_SYNC);
	if (dev->fd < 0) {
		printf("Open failed for file '%s': errno %d, %s\n",
			dev->filename, errno, strerror(errno));
		return -1;
	}

	/* PCI memory size */
	status = fstat(dev->fd, &statbuf);
	if (status < 0) {
		printf("fstat() failed: errno %d, %s\n",
			errno, strerror(errno));
		return -1;
	}
	dev->size = statbuf.st_size;

	/* Map */
	dev->maddr = (unsigned char *)mmap(
		NULL,
		(size_t)(dev->size),
		PROT_READ|PROT_WRITE,
		MAP_SHARED,
		dev->fd,
		0);
	if (dev->maddr == (unsigned char *)MAP_FAILED) {
//		printf("failed (mmap returned MAP_FAILED)\n");
		printf("BARs that are I/O ports are not supported by this tool\n");
		dev->maddr = 0;
		close(dev->fd);
		return -1;
	}

	/* Device regions smaller than a 4k page in size can be offset
	 * relative to the mapped base address. The offset is
	 * the physical address modulo 4k
	 */
	{
		char configname[100];
		int fd;

		snprintf(configname, 99, "/sys/bus/pci/devices/%04x:%02x:%02x.%1x/config",
				dev->domain, dev->bus, dev->slot, dev->function);
		fd = open(configname, O_RDWR | O_SYNC);
		if (dev->fd < 0) {
			printf("Open failed for file '%s': errno %d, %s\n",
				configname, errno, strerror(errno));
			return -1;
		}

		status = lseek(fd, 0x10 + 4*dev->bar, SEEK_SET);
		if (status < 0) {
			printf("Error: configuration space lseek failed\n");
			close(fd);
			return -1;
		}
		status = read(fd, &dev->phys, 4);
		if (status < 0) {
			printf("Error: configuration space read failed\n");
			close(fd);
			return -1;
		}
		dev->offset = ((dev->phys & 0xFFFFFFF0) % 0x1000);
		dev->addr = dev->maddr + dev->offset;
		close(fd);
	}


	/* ------------------------------------------------------------
	 * Tests
	 * ------------------------------------------------------------
	 */

	printf("\n");
	printf("PCI debug\n");
	printf("---------\n\n");
	printf(" - accessing BAR%d\n", dev->bar);
	printf(" - region size is %d-bytes\n", dev->size);
	printf(" - offset into region is %d-bytes\n", dev->offset);

	/* Display help */
	display_help(dev);

	/* Process commands */
	parse_command(dev);

	/* Cleanly shutdown */
	munmap(dev->maddr, dev->size);
	close(dev->fd);
	return 0;
}

void
parse_command(
	device_t *dev)
{
	char *line;
	int len;
	int status;

	while(1) {
		line = readline("PCI> ");
		/* Ctrl-D check */
		if (line == NULL) {
			printf("\n"); 
			continue;
		}
		/* Empty line check */
		len = strlen(line);
		if (len == 0) {
			continue;
		}
		/* Process the line */
		status = process_command(dev, line);
		if (status < 0) {
			break;
		}

		/* Add it to the history */
		add_history(line);
		free(line);
	}
	return;
}

/*--------------------------------------------------------------------
 * User interface
 *--------------------------------------------------------------------
 */
void
display_help(
	device_t *dev)
{
	printf("\n");
	printf("  ?                         Help\n");
	printf("  d[width] addr len         Display memory starting from addr\n");
	printf("                            [width]\n");
	printf("                              8   - 8-bit access\n");
	printf("                              16  - 16-bit access\n");
	printf("                              32  - 32-bit access (default)\n");
	printf("  c[width] addr val         Change memory at addr to val\n");
	printf("  e                         Print the endian access mode\n");
	printf("  e[mode]                   Change the endian access mode\n");
	printf("                            [mode]\n");
	printf("                              b - big-endian (default)\n");
	printf("                              l - little-endian\n");
	printf("  f[width] addr val len inc  Fill memory\n");
	printf("                              addr - start address\n");
	printf("                              val  - start value\n");
	printf("                              len  - length (in bytes)\n");
	printf("                              inc  - increment (defaults to 1)\n");
	printf("  q                          Quit\n");
	printf("\n  Notes:\n");
	printf("    1. addr, len, and val are interpreted as hex values\n");
	printf("       addresses are always byte based\n");
	printf("\n");
}
void pcie_mem_enable(void)
{
	system("setpci -s 1:0.0 4.b=6");
	system("setpci -s 1:0.0 5.b=0");
}
void pcie_link_down(void)
{
	system("setpci -s 0:1.0 3e.b=40:40");
	usleep(50000);
	system("setpci -s 0:1.0 3e.b=0:40");
	usleep(50000);
	pcie_mem_enable();
	printf("link down pass\n");
	
}
void pcie_speed_change_gen1(void)
{
	system("setpci -s 0:1.0 70.b=1");
	system("setpci -s 0:1.0 50.b=60");			
}
void pcie_speed_change_gen2(void)
{
	system("setpci -s 0:1.0 70.b=2");
	system("setpci -s 0:1.0 50.b=60");			
}
void desc_speed_reset_mix_case(device_t *dev)
{
	uint32_t i = 0;
	write_le32(dev, 0x34, 0x1100000);
	write_le32(dev, 0x2c, 10);
	while(0x40 == (read_le32(dev, 0x44) & 0x40));

	write_le32(dev, 0x1c, 0x11001e0);
	write_le32(dev, 0x14, 10);
	while(0x10 == (read_le32(dev, 0x44) & 0x10));

	i = memcmp((boot_buffer + sizeof(desc)), (boot_buffer + 0x2000), desc_data_size * 10);
	if(0 == i) {
		printf("desc pass \n");
	} else {
		mem_disp((void *)(boot_buffer+0x2000), desc_data_size * 10);
		mem_disp((void *)(boot_buffer+sizeof(desc)), desc_data_size * 10);
		//access(0,0);
	}	
	desc_data_size += 4;
	if(desc_data_size > 128) {
		desc_data_size = 4;
	}
	for(desc_size = 0, data_cnt = 0; desc_size < 20; desc_size += 2)
	{		
		desc[desc_size+1].Transfer_Size = desc_data_size;
		desc[desc_size+1].DAR_Low = ep_addr + (data_cnt * desc_data_size); 			
		desc[desc_size+1].SAR_Low = phys_addr + sizeof(desc) + (data_cnt++ * desc_data_size);				
	}

	for(desc_size = 20, data_cnt = 0; desc_size < 40; desc_size += 2)
	{		
		desc[desc_size+1].Transfer_Size = desc_data_size;
		desc[desc_size+1].DAR_Low = (phys_addr + 0x2000) + (data_cnt * desc_data_size);
		desc[desc_size+1].SAR_Low = ep_addr + (data_cnt++ * desc_data_size);
	}
	memset((boot_buffer), 0, sizeof(desc));
	memcpy((void *)(boot_buffer), desc, sizeof(desc));
	printf("desc size : %#x\n", desc_data_size);
	//mem_disp((void *)(boot_buffer), sizeof(desc));

	memset((boot_buffer + 0x2000), 0, desc_data_size);

}
int process_command(device_t *dev, char *cmd)
{
	if (cmd[0] == '\0') {
		return 0;
	}
	switch (cmd[0]) {
		case '?':
			display_help(dev);
			break;
		case 'c':
		case 'C':
			return change_mem(dev, cmd);
		case 'd':
		case 'D':
			return display_mem(dev, cmd);
		case 'e':
		case 'E':
			return change_endian(dev, cmd);
		case 'f':
		case 'F':
			return fill_mem(dev, cmd);
		case 'q':
		case 'Q':
			mem_disp((void *)(boot_buffer+0x2000), 0x500);
			mem_disp((void *)(boot_buffer), 0x500);
			return 1;
		case 'l':
		case 'L': 
			system("setpci -s 1:0.0 5.b=0");
			system("setpci -s 1:0.0 b3.b=0");
			system("setpci -s 1:0.0 52.b=0:1");
			printf("legacy int init\n");
			return 1;
		case 'x':
			system("setpci -s 1:0.0 5.b=04"); 	
			system("setpci -s 1:0.0 b3.b=0");
			system("setpci -s 1:0.0 52.b=1:1");
			printf("msi int init\n");
			return 1;
		case 'X':
			system("setpci -s 1:0.0 5.b=04");
			system("setpci -s 1:0.0 b3.b=80");
			printf("msix int init\n");
			return 1;
		case 'a':
			//printf("bar0 base addr %#x size %#x\n", dev->addr, dev->size);
			return 1;	
		case 'i':
			printf("bar0 aut init dma reg -> bar0\n");
			system("setpci -s 1:0.0 81c.b=0:1");
			usleep(50);
			system("setpci -s 1:0.0 81c.b=0:1");
			printf("enable AUT\n");
			usleep(1000);
			write_le32(dev, 0x108, 0xa3000000);
			write_le32(dev, 0x110, 0xa3010000);
			write_le32(dev, 0x114, 0x10d80000);
			write_le32(dev, 0x100, 0x0);
			write_le32(dev, 0x104, 0x80000000);

			printf("inbound base addr: %#x\n", read_le32(dev, 0x108));
			printf("inbound limit addr: %#x\n", read_le32(dev, 0x110));
			printf("inbound target addr: %#x\n", read_le32(dev, 0x114));
			printf("inbound ctrl 1: %#x\n", read_le32(dev, 0x0));
			printf("inbound ctrl 2: %#x\n", read_le32(dev, 0x104));

			usleep(1000);
			system("setpci -s 1:0.0 81c.b=1:1");
			usleep(50);
			system("setpci -s 1:0.0 81c.b=1:1");
			usleep(1000);
			printf("disable AUT\n");
			write_le32(dev, 0x0, 0x0);
			write_le32(dev, 0x8, 0x1);
			printf("dma init done\n");
			return 1;

		case '1':	//pre-fetch
			write_le32(dev, 0xc, 0x1100000);
			write_le32(dev, 0x14, 0x1100030);
			write_le32(dev, 0x4, 1);
			write_le32(dev, 0x8, 1);
			return 1;

		case '2':
			while(1){
				pcie_speed_change_gen1();				
				desc_speed_reset_mix_case(dev);
				pcie_speed_change_gen2();
				desc_speed_reset_mix_case(dev);
				//pcie_link_down();
			}
			return 1;
		case '4':
			return 1;
		default:
			break;
	}
	return 0;
}

int display_mem(device_t *dev, char *cmd)
{
	int width = 32;
	int addr = 0;
	int len = 0;
	int status;
	int i;
	unsigned char d8;
	unsigned short d16;
	unsigned int d32;

	/* d, d8, d16, d32 */
	if (cmd[1] == ' ') {
		status = sscanf(cmd, "%*c %x %x", &addr, &len);
		if (status != 2) {
			printf("Syntax error (use ? for help)\n");
			/* Don't break out of command processing loop */
			return 0;
		}
	} else {
		status = sscanf(cmd, "%*c%d %x %x", &width, &addr, &len);
		if (status != 3) {
			printf("Syntax error (use ? for help)\n");
			/* Don't break out of command processing loop */
			return 0;
		}
	}
	if (addr > dev->size) {
		printf("Error: invalid address (maximum allowed is %.8X\n", dev->size);
		return 0;
	}
	/* Length is in bytes */
	if ((addr + len) > dev->size) {
		/* Truncate */
		len = dev->size;
	}
	switch (width) {
		case 8:
			for (i = 0; i < len; i++) {
				if ((i%16) == 0) {
					printf("\n%.8X: ", addr+i);
				}
				d8 = read_8(dev, addr+i);
				printf("%.2X ", d8);
			}
			printf("\n");
			break;
		case 16:
			for (i = 0; i < len; i+=2) {
				if ((i%16) == 0) {
					printf("\n%.8X: ", addr+i);
				}
				if (big_endian == 0) {
					d16 = read_le16(dev, addr+i);
				} else {
					d16 = read_be16(dev, addr+i);
				}
				printf("%.4X ", d16);
			}
			printf("\n");
			break;
		case 32:
			for (i = 0; i < len; i+=4) {
				if ((i%16) == 0) {
					printf("\n%.8X: ", addr+i);
				}
				if (big_endian == 0) {
					d32 = read_le32(dev, addr+i);
				} else {
					d32 = read_be32(dev, addr+i);
				}
				printf("%.8X ", d32);
			}
			printf("\n");
			break;
		default:
			printf("Syntax error (use ? for help)\n");
			/* Don't break out of command processing loop */
			break;
	}
	printf("\n");
	return 0;
}

int change_mem(device_t *dev, char *cmd)
{
	int width = 32;
	int addr = 0;
	int status;
	unsigned char d8;
	unsigned short d16;
	unsigned int d32;

	/* c, c8, c16, c32 */
	if (cmd[1] == ' ') {
		status = sscanf(cmd, "%*c %x %x", &addr, &d32);
		if (status != 2) {
			printf("Syntax error (use ? for help)\n");
			/* Don't break out of command processing loop */
			return 0;
		}
	} else {
		status = sscanf(cmd, "%*c%d %x %x", &width, &addr, &d32);
		if (status != 3) {
			printf("Syntax error (use ? for help)\n");
			/* Don't break out of command processing loop */
			return 0;
		}
	}
	if (addr > dev->size) {
		printf("Error: invalid address (maximum allowed is %.8X\n", dev->size);
		return 0;
	}
	switch (width) {
		case 8:
			d8 = (unsigned char)d32;
			write_8(dev, addr, d8);
			break;
		case 16:
			d16 = (unsigned short)d32;
			if (big_endian == 0) {
				write_le16(dev, addr, d16);
			} else {
				write_be16(dev, addr, d16);
			}
			break;
		case 32:
			if (big_endian == 0) {
				write_le32(dev, addr, d32);
			} else {
				write_be32(dev, addr, d32);
			}
			break;
		default:
			printf("Syntax error (use ? for help)\n");
			/* Don't break out of command processing loop */
			break;
	}
	return 0;
}

int fill_mem(device_t *dev, char *cmd)
{
	int width = 32;
	int addr = 0;
	int len = 0;
	int inc = 0;
	int status;
	int i;
	unsigned char d8;
	unsigned short d16;
	unsigned int d32;

	/* c, c8, c16, c32 */
	if (cmd[1] == ' ') {
		status = sscanf(cmd, "%*c %x %x %x %x", &addr, &d32, &len, &inc);
		if ((status != 3) && (status != 4)) {
			printf("Syntax error (use ? for help)\n");
			/* Don't break out of command processing loop */
			return 0;
		}
		if (status == 3) {
			inc = 1;
		}
	} else {
		status = sscanf(cmd, "%*c%d %x %x %x %x", &width, &addr, &d32, &len, &inc);
		if ((status != 3) && (status != 4)) {
			printf("Syntax error (use ? for help)\n");
			/* Don't break out of command processing loop */
			return 0;
		}
		if (status == 4) {
			inc = 1;
		}
	}
	if (addr > dev->size) {
		printf("Error: invalid address (maximum allowed is %.8X\n", dev->size);
		return 0;
	}
	/* Length is in bytes */
	if ((addr + len) > dev->size) {
		/* Truncate */
		len = dev->size;
	}
	switch (width) {
		case 8:
			for (i = 0; i < len; i++) {
				d8 = (unsigned char)(d32 + i*inc);
				write_8(dev, addr+i, d8);
			}
			break;
		case 16:
			for (i = 0; i < len/2; i++) {
				d16 = (unsigned short)(d32 + i*inc);
				if (big_endian == 0) {
					write_le16(dev, addr+2*i, d16);
				} else {
					write_be16(dev, addr+2*i, d16);
				}
			}
			break;
		case 32:
			for (i = 0; i < len/4; i++) {
				if (big_endian == 0) {
					write_le32(dev, addr+4*i, d32 + i*inc);
				} else {
					write_be32(dev, addr+4*i, d32 + i*inc);
				}
			}
			break;
		default:
			printf("Syntax error (use ? for help)\n");
			/* Don't break out of command processing loop */
			break;
	}
	return 0;
}

int change_endian(device_t *dev, char *cmd)
{
	char endian = 0;
	int status;

	/* e, el, eb */
	status = sscanf(cmd, "%*c%c", &endian);
	if (status < 0) {
		/* Display the current setting */
		if (big_endian == 0) {
			printf("Endian mode: little-endian\n");
		} else {
			printf("Endian mode: big-endian\n");
		}
		return 0;
	} else if (status == 1) {
		switch (endian) {
			case 'b':
				big_endian = 1;
				break;
			case 'l':
				big_endian = 0;
				break;
			default:
				printf("Syntax error (use ? for help)\n");
				/* Don't break out of command processing loop */
				break;
		}
	} else {
		printf("Syntax error (use ? for help)\n");
		/* Don't break out of command processing loop */
	}
	return 0;
}

/* ----------------------------------------------------------------
 * Raw pointer read/write access
 * ----------------------------------------------------------------
 */
static void
write_8(
	device_t      *dev,
	unsigned int   addr,
	unsigned char  data)
{
	*(volatile unsigned char *)(dev->addr + addr) = data;
	msync((void *)(dev->addr + addr), 1, MS_SYNC | MS_INVALIDATE);
}

static unsigned char
read_8(
	device_t      *dev,
	unsigned int   addr)
{
	return *(volatile unsigned char *)(dev->addr + addr);
}

static void
write_le16(
	device_t      *dev,
	unsigned int   addr,
	unsigned short int data)
{
	if (__BYTE_ORDER != __LITTLE_ENDIAN) {
		data = bswap_16(data);
	}
	*(volatile unsigned short int *)(dev->addr + addr) = data;
	msync((void *)(dev->addr + addr), 2, MS_SYNC | MS_INVALIDATE);
}

static unsigned short int
read_le16(
	device_t      *dev,
	unsigned int   addr)
{
	unsigned int data = *(volatile unsigned short int *)(dev->addr + addr);
	if (__BYTE_ORDER != __LITTLE_ENDIAN) {
		data = bswap_16(data);
	}
	return data;
}

static void
write_be16(
	device_t      *dev,
	unsigned int   addr,
	unsigned short int data)
{
	if (__BYTE_ORDER == __LITTLE_ENDIAN) {
		data = bswap_16(data);
	}
	*(volatile unsigned short int *)(dev->addr + addr) = data;
	msync((void *)(dev->addr + addr), 2, MS_SYNC | MS_INVALIDATE);
}

static unsigned short int
read_be16(
	device_t      *dev,
	unsigned int   addr)
{
	unsigned int data = *(volatile unsigned short int *)(dev->addr + addr);
	if (__BYTE_ORDER == __LITTLE_ENDIAN) {
		data = bswap_16(data);
	}
	return data;
}

static void
write_le32(
	device_t      *dev,
	unsigned int   addr,
	unsigned int data)
{
	usleep(1);
	if (__BYTE_ORDER != __LITTLE_ENDIAN) {
		data = bswap_32(data);
	}
	*(volatile unsigned int *)(dev->addr + addr) = data;
	msync((void *)(dev->addr + addr), 4, MS_SYNC | MS_INVALIDATE);
}

static unsigned int
read_le32(
	device_t      *dev,
	unsigned int   addr)
{
	unsigned int data = *(volatile unsigned int *)(dev->addr + addr);
	if (__BYTE_ORDER != __LITTLE_ENDIAN) {
		data = bswap_32(data);
	}
	return data;
}

static void
write_be32(
	device_t      *dev,
	unsigned int   addr,
	unsigned int data)
{
	if (__BYTE_ORDER == __LITTLE_ENDIAN) {
		data = bswap_32(data);
	}
	*(volatile unsigned int *)(dev->addr + addr) = data;
	msync((void *)(dev->addr + addr), 4, MS_SYNC | MS_INVALIDATE);
}

static unsigned int
read_be32(
	device_t      *dev,
	unsigned int   addr)
{
	unsigned int data = *(volatile unsigned int *)(dev->addr + addr);
	if (__BYTE_ORDER == __LITTLE_ENDIAN) {
		data = bswap_32(data);
	}
	return data;
}

