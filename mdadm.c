#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cache.h"
#include "jbod.h"
#include "mdadm.h"
#include "net.h"

int is_mounted = 0;
int is_written = 0;
int write_permission = 0;

/* the create_opcode function is used to simplify the task of creating the opcode and reducing
   redundant code.*/
uint32_t create_opcode(uint32_t DiskID, uint32_t BlockID, uint32_t Command, uint32_t Reserved) {
	uint32_t opcode = Reserved;
	opcode = (opcode << 6) | Command;
	opcode = (opcode << 4) | DiskID;
	opcode = (opcode << 8) | BlockID;

	return opcode;
}

/* This function mounts the disk by calling the jbod_operation function.  is_mounted is alos updated
   to reflect the changes. */
int mdadm_mount(void) {
	int result = jbod_operation(create_opcode(0,0,JBOD_MOUNT,0), NULL);
	if (result == 0) {
		is_mounted = 1;
		return 1;
	}
	else return -1;
}

/* This function unmounts the disk by calling the jbod_operation function.  is_mounted is also updated
   to reflect the changes. */
int mdadm_unmount(void) {
	int result = jbod_operation(create_opcode(0,0,JBOD_UNMOUNT,0), NULL);
	if (result == 0) {
		is_mounted = 0;
		return 1;
	}
	else return -1;
}

/* This function enables write permissions by invoking the JBOD function. */
int mdadm_write_permission(void) {
	int r = jbod_operation(create_opcode(0,0,JBOD_WRITE_PERMISSION,0),NULL);
	if (r == 0) {
		write_permission = 1;
	}
	//printf("Write permissions is %d and write_permission is now %d\n",r,write_permission);
	return r;
}

/* This function revokes write permissions by invoking the JBOD function. */
int mdadm_revoke_write_permission(void) {
	int r = jbod_operation(create_opcode(0,0,JBOD_REVOKE_WRITE_PERMISSION,0),NULL);
	if (r == 0) {
		write_permission = 0;
	}
	return r;
}

int mdadm_read(uint32_t start_addr, uint32_t read_len, uint8_t *read_buf)  {

	/* The below 5 if statements checks that the inputted parameters are met and that the disk
	   is mounted. */
	if (is_mounted == 0) {
		return -1;
	}

	if (read_buf == NULL && read_len == 0) {
		return 0;
	}

	if (start_addr < 0 || start_addr + read_len - 1 >= 16*JBOD_DISK_SIZE) {
		return -1;
	}

	if (read_len > 1024) {
		return -1;
	}

	if (read_buf == NULL && read_len != 0) {
		return -1;
	}

	// Determining start and end disks and blocks for use later in the program.
	uint32_t start_disk = start_addr / JBOD_DISK_SIZE;
	uint32_t end_disk = (start_addr + read_len - 1) / JBOD_DISK_SIZE;
	uint32_t start_block = start_addr % JBOD_DISK_SIZE / JBOD_BLOCK_SIZE;
	uint32_t end_block = ((start_addr + read_len - 1) % JBOD_DISK_SIZE) / JBOD_BLOCK_SIZE;

	// Initial Seek to Disk
	jbod_operation(create_opcode(start_disk,0,JBOD_SEEK_TO_DISK,0),NULL);

	// Initial Seek to Block
	jbod_operation(create_opcode(0,start_block,JBOD_SEEK_TO_BLOCK,0),NULL);

	/* the c_block and c_disk variables are initialized in order to keep track of the current
	   block and disk.  The c_pointer is used to keep track of the current position within the
	   read buffer.  The read variable tracks the number of bytes read until now. */
	int c_block = start_block;
	int c_disk = start_disk;
	uint8_t* c_pointer = read_buf;
	int read = 0;

	/* This loop keeps repeating until the number of bytes read equals the length of what we want
	   to read. */
	while (read < read_len) {

		/* This block executes when reading within a singular block. */
		if (start_disk == end_disk && start_block == end_block) {
			int start_pos = start_addr % JBOD_BLOCK_SIZE;
			int end_pos = (start_addr + read_len - 1) % JBOD_BLOCK_SIZE;
			int len = ((JBOD_BLOCK_SIZE-start_pos+1)-(JBOD_BLOCK_SIZE-end_pos-1) - 1);
			uint8_t temp[JBOD_BLOCK_SIZE];
			if (cache_lookup(start_disk,start_block,temp) == 1) {
				jbod_operation(create_opcode(0,start_block+1,JBOD_SEEK_TO_BLOCK,0),NULL);
			}
			else {
			jbod_operation(create_opcode(0,0,JBOD_READ_BLOCK,0),temp);
			}
			int j = 0;
			for (int i = start_pos; i <= end_pos; i++) {
				c_pointer[j] = temp[i];
				j++;
			}
			c_pointer += len;
			read += len;
		}
		
		/* This block executes on the first iteration of a multi-block read, accounting for
		   the edge case where the first byte lies within a block. */
		else if (start_disk == c_disk && start_block == c_block) {	
			int start_pos = start_addr % JBOD_BLOCK_SIZE;
			int len = JBOD_BLOCK_SIZE-start_pos;
			uint8_t temp[JBOD_BLOCK_SIZE];
			if (cache_lookup(c_disk,c_block,temp) == 1) {
				jbod_operation(create_opcode(0,c_block+1,JBOD_SEEK_TO_BLOCK,0),NULL);
			}
			else {
				jbod_operation(create_opcode(0,0,JBOD_READ_BLOCK,0),temp);
			}
			int j = 0;
			for (int i = start_pos; i < JBOD_BLOCK_SIZE; i++) {
				c_pointer[j] = temp[i];
				j++;
			}
			c_pointer += (len);
			read += len;
		}

		/* This block executes on the last iteration of a multi-block read, accounting for
		   the edge case where the last byte lies within a block. */
		else if (end_disk == c_disk && end_block == c_block) {
			int end_pos = (start_addr + read_len - 1) % JBOD_BLOCK_SIZE;
			uint8_t temp[JBOD_BLOCK_SIZE];
			if (cache_lookup(c_disk,c_block,temp) == 1) {
				jbod_operation(create_opcode(0,c_block+1,JBOD_SEEK_TO_BLOCK,0),NULL);
			}
			else {
				jbod_operation(create_opcode(0,0,JBOD_READ_BLOCK,0),temp);
			}
			for (int i = 0; i <= end_pos; i++) {
				c_pointer[i] = temp[i];
			}
			c_pointer += (end_pos + 1);
			read += (end_pos + 1);
		}

		/* This block executes when the entire block needs to be read.  It accounts for the
		   majority of block reads. */
		else {
			uint8_t temp[JBOD_BLOCK_SIZE];
			if (cache_lookup(c_disk,c_block,temp) == 1) {
				jbod_operation(create_opcode(0,c_block+1,JBOD_SEEK_TO_BLOCK,0),NULL);
			}
			else {
				jbod_operation(create_opcode(0,0,JBOD_READ_BLOCK,0),temp);
			}
			for (int i = 0; i < JBOD_BLOCK_SIZE; i++) {
				c_pointer[i] = temp[i];
			}
			c_pointer += JBOD_BLOCK_SIZE;
			read += JBOD_BLOCK_SIZE;

		}

		/* The if statement below iterates the current block and disk based on the value of
		   the current block.  This allows multi-block and multi-disk reads to occur. */
		c_block++;
		if (c_block > 255) {
			c_block = 0;
			c_disk += 1;
			jbod_operation(create_opcode(c_disk,0,JBOD_SEEK_TO_DISK,0),NULL);
		}
	}
	return read_len;
}


/* This loop keeps repeating until the number of bytes written equals the length of what we want
   to write. */
int mdadm_write(uint32_t start_addr, uint32_t write_len, const uint8_t *write_buf) {

	/* The below 5 if statements checks that the inputted parameters are met and that the disk
	   is mounted. */
	if (is_mounted == 0) {
		return -1;
	}

	if (write_permission == 0) {
		return -1;
	} 

	if (write_buf == NULL && write_len != 0) {
		return -1;
	}

	if (write_buf == NULL && write_len == 0) {
		return 0;
	}

	if (start_addr < 0 || start_addr + write_len - 1 >= 16*JBOD_DISK_SIZE) {
		return -1;
	}

	if (write_len > 1024) {
		return -1;
	}

	if (write_buf == NULL && write_len != 0) {
		return -1;
	}

	// Determining start and end disks and blocks for use later in the program.
	uint32_t start_disk = start_addr / ((uint32_t) JBOD_DISK_SIZE);
	uint32_t end_disk = (start_addr + write_len - 1) / ((uint32_t) JBOD_DISK_SIZE);
	uint32_t start_block = start_addr % JBOD_DISK_SIZE / JBOD_BLOCK_SIZE;
	uint32_t end_block = ((start_addr + write_len - 1) % JBOD_DISK_SIZE) / JBOD_BLOCK_SIZE;

	// Initial Seek to Disk
	jbod_operation(create_opcode(start_disk,0,JBOD_SEEK_TO_DISK,0),NULL);

	// Initial Seek to Block
	jbod_operation(create_opcode(0,start_block,JBOD_SEEK_TO_BLOCK,0),NULL);

	/* the c_block and c_disk variables are initialized in order to keep track of the current
	block and disk.  The c_pointer is used to keep track of the current position within the
	read buffer.  The write variable tracks the number of bytes write until now. */
	int c_block = start_block;
	int c_disk = start_disk;
	uint8_t* c_pointer = (uint8_t*) write_buf;
	int write = 0;
	uint8_t temp_buffer[256];

	while (write < write_len) {
		/* This block executes when writing within a singular block. */
		if (start_disk == end_disk && start_block == end_block) {
			int start_pos = start_addr % JBOD_BLOCK_SIZE;
			int end_pos = (start_addr + write_len - 1) % JBOD_BLOCK_SIZE;
			int len = ((JBOD_BLOCK_SIZE-start_pos+1)-(JBOD_BLOCK_SIZE-end_pos-1) - 1);
			uint8_t read_temp[JBOD_BLOCK_SIZE];
			uint8_t write_temp[JBOD_BLOCK_SIZE];
			if (cache_lookup(c_disk,c_block,read_temp) == 1) {
				jbod_operation(create_opcode(0,c_block+1,JBOD_SEEK_TO_BLOCK,0),NULL);
			}
			else {
				jbod_operation(create_opcode(0,0,JBOD_READ_BLOCK,0),read_temp);
			}
			for (int i = 0; i < start_pos; i++) {
				write_temp[i] = read_temp[i];
			}
			for (int i = end_pos + 1; i < JBOD_BLOCK_SIZE; i++) {
				write_temp[i] = read_temp[i];
			}
			int j = 0;
			for (int i = start_pos; i <= end_pos; i++) {
				write_temp[i] = c_pointer[j];
				j++;
			}
			jbod_operation(create_opcode(0,c_block,JBOD_SEEK_TO_BLOCK,0),NULL);
			if (cache_lookup(c_disk,c_block,temp_buffer) == 1) {
				cache_update(c_disk,c_block,write_temp);
			}
			else {
				cache_insert(c_disk,c_block,write_temp);
			}
			jbod_operation(create_opcode(0,0,JBOD_WRITE_BLOCK,0),write_temp);
			c_pointer += len;
			write += len;
		}

		/* This block executes on the first iteration of a multi-block write, accounting for
		   the edge case where the first byte lies within a block. */
		else if (start_disk == c_disk && start_block == c_block) {	
			int start_pos = start_addr % JBOD_BLOCK_SIZE;
			int len = JBOD_BLOCK_SIZE-start_pos;
			uint8_t read_temp[JBOD_BLOCK_SIZE];
			uint8_t write_temp[JBOD_BLOCK_SIZE];
			if (cache_lookup(c_disk,c_block,read_temp) == 1) {
				jbod_operation(create_opcode(0,c_block+1,JBOD_SEEK_TO_BLOCK,0),NULL);
			}
			else {
				jbod_operation(create_opcode(0,0,JBOD_READ_BLOCK,0),read_temp);
			}
			for (int i = 0; i < start_pos; i++) {
				write_temp[i] = read_temp[i];
			}
			jbod_operation(create_opcode(0,c_block,JBOD_SEEK_TO_BLOCK,0),NULL);
			int j = 0;
			for (int i = start_pos; i < JBOD_BLOCK_SIZE; i++) {
				write_temp[i] = c_pointer[j];
				j++;
			}
			if (cache_lookup(c_disk,c_block,temp_buffer) == 1) {
				cache_update(c_disk,c_block,write_temp);
			}
			else {
				cache_insert(c_disk,c_block,write_temp);
			}
			jbod_operation(create_opcode(0,0,JBOD_WRITE_BLOCK,0),write_temp);
			c_pointer += (len);
			write += len;
		}

		/* This block executes on the last iteration of a multi-block write, accounting for
		   the edge case where the last byte lies within a block. */
		else if (end_disk == c_disk && end_block == c_block) {
			int end_pos = (start_addr + write_len - 1) % JBOD_BLOCK_SIZE;
			uint8_t write_temp[JBOD_BLOCK_SIZE];
			uint8_t read_temp[JBOD_BLOCK_SIZE];
			if (cache_lookup(c_disk,c_block,read_temp) == 1) {
				jbod_operation(create_opcode(0,c_block+1,JBOD_SEEK_TO_BLOCK,0),NULL);
			}
			else {
				jbod_operation(create_opcode(0,0,JBOD_READ_BLOCK,0),read_temp);
			}
			for (int i = 0; i <= end_pos; i++) {
				write_temp[i] = c_pointer[i];
			}
			jbod_operation(create_opcode(0,end_block,JBOD_SEEK_TO_BLOCK,0),NULL);
			for (int i = end_pos + 1; i < JBOD_BLOCK_SIZE; i++) {
				write_temp[i] = read_temp[i];
			}
			if (cache_lookup(c_disk,c_block,temp_buffer) == 1) {
				cache_update(c_disk,c_block,write_temp);
			}
			else {
				cache_insert(c_disk,c_block,write_temp);
			}
			jbod_operation(create_opcode(0,0,JBOD_WRITE_BLOCK,0),write_temp);
			c_pointer += (end_pos + 1);
			write += (end_pos + 1);
		}
		
		/* This block executes when the entire block needs to be written.  It accounts for the
		   majority of block writes. */
		else {
			uint8_t temp[JBOD_BLOCK_SIZE];
			for (int i = 0; i < JBOD_BLOCK_SIZE; i++) {
				temp[i] = c_pointer[i];
			}
			if (cache_lookup(c_disk,c_block,temp_buffer) == 1) {
				cache_update(c_disk,c_block,temp);
			}
			else {
				cache_insert(c_disk,c_block,temp);
			}
			jbod_operation(create_opcode(0,0,JBOD_WRITE_BLOCK,0),temp);
			c_pointer += JBOD_BLOCK_SIZE;
			write += JBOD_BLOCK_SIZE;

		}

		/* The if statement below iterates the current block and disk based on the value of
		   the current block.  This allows multi-block and multi-disk writes to occur. */
		c_block++;
		if (c_block > JBOD_BLOCK_SIZE - 1) {
			c_block = 0;
			c_disk += 1;
			jbod_operation(create_opcode(c_disk,0,JBOD_SEEK_TO_DISK,0),NULL);
		}
	}
	return write_len;
}

