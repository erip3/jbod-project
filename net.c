#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <err.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include "net.h"
#include "jbod.h"

/* the client socket descriptor for the connection to the server */
int cli_sd = -1;

/* helper function to pack JBOD protocol packets */
void pack_JBOD_packet(uint32_t op, uint8_t ret, uint8_t *packet) {

    // 0 out packet data
    memset(packet, 0, HEADER_LEN);

    // Insert op code and info code
    packet[0] = (op >> 24) & 0xFF;
    packet[1] = (op >> 16) & 0xFF;
    packet[2] = (op >> 8) & 0xFF;
    packet[3] = op & 0xFF;
    packet[4] = ret;
}

/* attempts to read n bytes from fd; returns true on success and false on
 * failure */
bool nread(int fd, int len, uint8_t *buf) {

    int total_bytes_read = 0; // Bytes read in total
    int bytes_read = 0; // Bytes read on one read operation

    // Read len number of bytes from the server and store in buf
    while (total_bytes_read < len) {
        bytes_read = read(fd, buf + total_bytes_read, len - total_bytes_read);
        if (bytes_read <= 0) return false;
        total_bytes_read += bytes_read;
    }

    return true;
}

/* attempts to write n bytes to fd; returns true on success and false on
 * failure */
bool nwrite(int fd, int len, uint8_t *buf) {

    int total_bytes_written = 0; // Bytes written in total
    int bytes_written = 0; // Bytes written in one write operation

    // Write len number of bytes from buf to the server
    while (total_bytes_written < len) {
        bytes_written = write(fd, buf + total_bytes_written, len - total_bytes_written);
        if (bytes_written <= 0) return false;
        total_bytes_written += bytes_written;
    }

    return true;
}

/* attempts to receive a packet from fd; returns true on success and false on
 * failure */
bool recv_packet(int fd, uint32_t *op, uint8_t *ret, uint8_t *block) {

  uint8_t response[JBOD_BLOCK_SIZE];

  // Read header from response packet
  if ( read(fd, response, HEADER_LEN) != HEADER_LEN ) return false;

  // Get opcode and and info code from response
  memcpy(op, &response[0], 4);
  *ret = response[4];

  // If ret indicates we have a payload, we read an additional 
  // JBOD_BLOCK_SIZE bytes using the nread function
  if ( *ret == 0b00000010 ) {
    nread(fd, JBOD_BLOCK_SIZE, block);
  }

  return true;
}

/* attempts to send a packet to sd; returns true on success and false on
 * failure */
bool send_packet(int fd, uint32_t op, uint8_t *block) {

  // If there is a payload, indicate this with the info code
  uint8_t ret = 0b00000000;
  if (block) ret = 0b00000010;

  // Create a packet to send to the server
  uint8_t packet[HEADER_LEN]; 
  pack_JBOD_packet(op, ret, packet);

  // Send packet to server
  if ( write(fd, packet, sizeof(packet)) != sizeof(packet) ) {
    return false;
  }

  // Write data if payload exists
  if ( ret == 0b00000010 ) {
    nwrite(fd, JBOD_BLOCK_SIZE, block);
  }

  return true;
}


/* connect to server and set the global client variable to the socket */
bool jbod_connect(const char *ip, uint16_t port) {

  struct sockaddr_in caddr;

  caddr.sin_family = AF_INET;
  caddr.sin_port = htons(port);
  if ( inet_aton(ip, &caddr.sin_addr) == 0 ) return false;

  cli_sd = socket(PF_INET, SOCK_STREAM, 0);
  if (cli_sd == -1) return false;

  if ( connect(cli_sd, (const struct sockaddr *)&caddr, sizeof(caddr)) == -1 ) {
    return false;
  }

  return true;
}

void jbod_disconnect(void) {
  close(cli_sd);
  cli_sd = -1;
}

int jbod_client_operation(uint32_t op, uint8_t *block) {

  // response_op and response_ret will hold the op code and info code
  // of the received JBOD 
  uint32_t response_op;
  uint8_t response_ret;

  // send and receive JBOD protocol packet 
  // if sending or receiving fails, return -1
  if ( !send_packet(cli_sd, op, block) ) return -1;
  if ( !recv_packet(cli_sd, &response_op, &response_ret, block) ) return -1;

  return 0;
}