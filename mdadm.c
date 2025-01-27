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

// Packs together jbod operation fields into one 32-bit number
//
uint32_t pack_operation(int disk_id, int block_id, int command) {
  uint32_t result = 0x0, temp_disk_id, temp_block_id, temp_command;

  temp_disk_id = disk_id & 0xF;
  temp_block_id = (block_id & 0xFF) << 4;
  temp_command = (command & 0xFF) << 12; // Shift so bits 12-19 are occupied by command
  result = temp_disk_id | temp_block_id | temp_command;

  return result;
}

// Check validity of arguments to read and write
//
int check_args(uint32_t addr, uint32_t len, const uint8_t *buf) {
  if ((addr + len) > (JBOD_NUM_DISKS * JBOD_NUM_BLOCKS_PER_DISK * JBOD_BLOCK_SIZE)) {
    return -1;
  } else if (len > 1024) {
    return -2;
  } else if (buf == NULL && len != 0) {
    return -4;
  } else if (len == 0) {
    return 0;
  }
  return 1;
}

// Position jbod at proper disk and block id
//
int position_jbod(uint32_t addr, int *disk_id, int *block_id) {
  // Calculate disk_id and block_id
  // 1MB / 16 Disks = 65536 Bytes per Disk
  // 65536 Bytes per Disk / 256 Blocks per Disk = 256 Bytes per Block
  *disk_id = addr / JBOD_DISK_SIZE;
  *block_id = (addr % JBOD_DISK_SIZE) / JBOD_BLOCK_SIZE;

  // Move to the specified disk
  int result = jbod_client_operation(pack_operation(*disk_id, 0, JBOD_SEEK_TO_DISK), NULL);

  // If operation was a failure, return proper error value.
  if (result == -1 && jbod_error == JBOD_UNMOUNTED) {
    return -3;
  } else if (result == -1) {
    return -1;
  }

  // Move to specified block
  result = jbod_client_operation(pack_operation(0, *block_id, JBOD_SEEK_TO_BLOCK), NULL);

  // If operation was a failure, return error value
  if (result == -1) return -1;

  return 1;
}

// Calculates which byte a given starting address references in a block
//
uint32_t get_in_block_address (uint32_t addr, int disk_id, int block_id) {
  uint32_t result = addr - (disk_id * JBOD_DISK_SIZE); // Get byte location in disk
  result = result % JBOD_BLOCK_SIZE; // Get byte location in block
  return result;
}

// Calculate bytes that will be read or written
//
int calc_bytes_used(int bytes_to_use, uint32_t in_block_address) {
    // If the number of bytes to read is at least 256 minus the current byte address:
    //    The number of bytes read is equal to 256 minus the current byte address.
    // Otherwise:
    //    The number of bytes read is equal to the number of remaining bytes to be read
    return (bytes_to_use >= JBOD_BLOCK_SIZE - in_block_address) 
      ? JBOD_BLOCK_SIZE - in_block_address
      : bytes_to_use;
}

int mdadm_mount(void) {
  // Generate and execute 32-bit command to mount disk
  const uint32_t op = pack_operation(0, 0, JBOD_MOUNT); 
  int result = jbod_client_operation(op, NULL); // Store success state in result

  // If operation was a failure, return -1. Return 1 otherwise.
  if (result == -1) return -1;
  return 1;
}

int mdadm_unmount(void) {
  // Generate and execute 32-bit command to unmount disk
  const uint32_t op = pack_operation(0, 0, JBOD_UNMOUNT);
  int result = jbod_client_operation(op, NULL);  // Store success state in result

  // If operation was a failure, return -1. Return 1 otherwise.
  if (result == -1) return -1;
  return 1;
}

int mdadm_write_permission(void) {
  // Generate and execute 32-bit command to set write permission
  const uint32_t op = pack_operation(0, 0, JBOD_WRITE_PERMISSION);
  int result = jbod_client_operation(op, NULL);  // Store success state in result

  // If user doesn't have write permission, return -5. Return 0 otherwise.
  if (result == -1) return -5;
	return 0;
}

int mdadm_revoke_write_permission(void) {
  // Generate and execute 32-bit command to set write permission
  const uint32_t op = pack_operation(0, 0, JBOD_REVOKE_WRITE_PERMISSION);
  int result = jbod_client_operation(op, NULL);  // Store success state in result

  // If operation was a failure, return -1. Return 0 otherwise.
  if (result == -1) return -1;
	return 0;
}

// Read given number of bytes from an address
//
int mdadm_read(uint32_t addr, uint32_t len, uint8_t *buf)
{
  // Ensure arguments are valid
  int result = check_args(addr, len, buf);
  if (result <= 0) return result;

  // Position JBOD at correct address
  int disk_id = 0, block_id = 0;
  result = position_jbod(addr, &disk_id, &block_id);
  if (result <= 0) return result;

  // Read operation 
  uint8_t temp_buf[JBOD_BLOCK_SIZE]; // Temporary buffer to hold values read
  int bytes_to_read = len; // Number of bytes remaining to read
  int bytes_read = 0; // Number of bytes read from one read operation
  int total_bytes_read = 0; // Number of bytes read in total
  uint32_t in_block_address = 
    get_in_block_address(addr, disk_id, block_id); // Address of current byte
                                                         // relative to current block

  while (total_bytes_read != len) 
  {
    // Increment disk ID if the end of the disk has been reached
    if (block_id >= JBOD_NUM_BLOCKS_PER_DISK) {
      result = jbod_client_operation(pack_operation(++disk_id, 0, JBOD_SEEK_TO_DISK), NULL);
      block_id = 0; // Reset block id
      jbod_client_operation(pack_operation(0, block_id, JBOD_SEEK_TO_BLOCK), NULL);
      if (result == -1) return -1; // If operation was a failure, return error value
    }

    // Calculate bytes that will be read
    bytes_read = calc_bytes_used(bytes_to_read, in_block_address);

    // Execute read operation
    if (cache_enabled()) 
    { // If the cache is available
      result = cache_lookup(disk_id, block_id, temp_buf);
      if (result != 1) 
      { // If block not in cache, insert it
        jbod_client_operation(pack_operation(block_id , 0, JBOD_SEEK_TO_BLOCK), NULL);
        result = jbod_client_operation(pack_operation(0, 0, JBOD_READ_BLOCK), temp_buf);
        if (result == -1) return -1; // If operation was a failure, return error value
      } 
    }
    else
    { // If the cache is not available
      result = jbod_client_operation(pack_operation(0, 0, JBOD_READ_BLOCK), temp_buf);
      if (result == -1) return -1; // If operation was a failure, return error value
    }

    memcpy(buf + total_bytes_read, temp_buf, bytes_read);

    total_bytes_read += bytes_read; // Increment total_bytes_read by bytes_read
    bytes_to_read -= bytes_read; // Bytes to read remaining
    in_block_address = 0; // Reset address for next block
    block_id++; // Increment block id
  };

  return len;
}

// Write specified number of bytes to an address
//
int mdadm_write(uint32_t addr, uint32_t len, const uint8_t *buf) 
{
  // Ensure arguments are valid
  int result = check_args(addr, len, buf);
  if (result <= 0) return result;

  // Position JBOD at correct address
  int disk_id = 0, block_id = 0;
  result = position_jbod(addr, &disk_id, &block_id);
  if (result <= 0) return result;

  // Write operation
  uint8_t temp_buf[JBOD_BLOCK_SIZE]; // Temporary buffer to hold values to write
  int bytes_to_write = len; // Number of bytes remaining to write
  int bytes_written = 0; // Number of bytes written from one write operation
  int total_bytes_written = 0; // Number of bytes written in total
  uint32_t in_block_address = 
    get_in_block_address(addr, disk_id, block_id); // Address of current byte
                                                   // relative to current block

  while (total_bytes_written != len) {

    // Increment disk ID if the end of the disk has been reached
    if (block_id >= JBOD_NUM_BLOCKS_PER_DISK) {
      result = jbod_client_operation(pack_operation(++disk_id, 0, JBOD_SEEK_TO_DISK), NULL);
      block_id = 0; // Reset block id
      jbod_client_operation(pack_operation(0, block_id, JBOD_SEEK_TO_BLOCK), NULL);
      if (result == -1) return -1; // If operation was a failure, return error value
    }

    // Calculate bytes that will be written:
    bytes_written = calc_bytes_used(bytes_to_write, in_block_address);

    // Execute write operation
    // We must preserve original data if we write to only part of a block
    if (in_block_address != 0 || bytes_written != JBOD_BLOCK_SIZE) {
      result = cache_lookup(disk_id, block_id, temp_buf);
      if (result != 1) 
      {
        result = jbod_client_operation(pack_operation(0, 0, JBOD_READ_BLOCK), temp_buf);
        jbod_client_operation(pack_operation(0, block_id, JBOD_SEEK_TO_BLOCK), NULL);
        cache_insert(disk_id, block_id, temp_buf);
      } 
      if (result == -1) return -1; // If operation was a failure, return error value
    }

    // temp_buf + in_block_address (only write to necessary parts of the block)
    // buf + total_bytes_written (get the correct bytes to be used)
    memcpy(temp_buf + in_block_address, buf + total_bytes_written, bytes_written);
    result = jbod_client_operation(pack_operation(0, 0, JBOD_WRITE_BLOCK), temp_buf);

    // If operation was a failure, return error value
    if (result == -1 && JBOD_WRITE_PERMISSION_ALREADY_REVOKED) {
      return -5;
    } else if (result == -1) {
      return -1;
    }

    if (cache_enabled()) 
    { // If cache is available, we will also write to it
      cache_insert(disk_id, block_id, temp_buf);
    }

    total_bytes_written += bytes_written; // Increment total_bytes_read by bytes_read
    bytes_to_write -= bytes_written; // Bytes to read remaining
    in_block_address = 0; // Reset address for next block
    block_id++; // Increment block id
  };

	return len;
}
