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

/* attempts to read n (len) bytes from fd; returns true on success and false on failure. 
It may need to call the system call "read" multiple times to reach the given size len. 
*/
static bool nread(int fd, int len, uint8_t *buf) {
  int bytes_read = 0;  // This variable accounts for the bytes read until a certain point.
  
  while (bytes_read < len) {  // While loop is in place to make sure all bytes are read.
    int bytes = read(fd, &buf[bytes_read], len-bytes_read);
    bytes_read += bytes;  // Number of bytes read until this point is tracked.
    if (bytes_read >= len) { // Makes sure that the while loop is terminated when the correct number of bytes is read.
      return true;
    }
  }
  return true;
}

/* attempts to write n bytes to fd; returns true on success and false on failure 
It may need to call the system call "write" multiple times to reach the size len.
*/
static bool nwrite(int fd, int len, uint8_t *buf) {
  int bytes_written = 0; // This variable accounts for the bytes written to until a certain point.
  
  while (bytes_written < len) { // While loop is in place to make sure all bytes are written.
    int bytes = write(fd, &buf[bytes_written], len-bytes_written);
    bytes_written += bytes; // Number of bytes written until this point is tracked.
    if (bytes_written >= len) { // Makes sure that the while loop is terminated when the correct number of bytes is written to.
      return true;
    }
  }
  return true;
}

/* Through this function call the client attempts to receive a packet from sd 
(i.e., receiving a response from the server.). It happens after the client previously 
forwarded a jbod operation call via a request message to the server.  
It returns true on success and false on failure. 
The values of the parameters (including op, ret, block) will be returned to the caller of this function: 

op - the address to store the jbod "opcode"  
ret - the address to store the info code (lowest bit represents the return value of the server side calling the corresponding jbod_operation function. 2nd lowest bit represent whether data block exists after HEADER_LEN.)
block - holds the received block content if existing (e.g., when the op command is JBOD_READ_BLOCK)

In your implementation, you can read the packet header first (i.e., read HEADER_LEN bytes first), 
and then use the length field in the header to determine whether it is needed to read 
a block of data from the server. You may use the above nread function here.  
*/
static bool recv_packet(int sd, uint32_t *op, uint8_t *ret, uint8_t *block) {
  uint8_t* header = malloc(HEADER_LEN);  // Header is malloced to provide space for the read operation to execute.

  if (!nread(sd, HEADER_LEN, header)) { // The nread is wrapped in the if statement to account for fails.
    free(header);
    return false;
  }

  memcpy(op,header,4);
  *op = ntohl(*op);  // The opcode is converted back to a regular byte.
  memcpy(ret,&header[4],1);

  if ((*ret >> 1) == 0) {  // This bitshift is to access the second-last bit of the ret byte, and determine if we need to access data.
    free(header);
    return true;
  }

  nread(sd,JBOD_BLOCK_SIZE,block); // Rest of the read operation is conducted.
  free(header);
  return true;
}



/* The client attempts to send a jbod request packet to sd (i.e., the server socket here); 
returns true on success and false on failure. 

op - the opcode. 
block- when the command is JBOD_WRITE_BLOCK, the block will contain data to write to the server jbod system;
otherwise it is NULL.

The above information (when applicable) has to be wrapped into a jbod request packet (format specified in readme).
You may call the above nwrite function to do the actual sending.  
*/
static bool send_packet(int sd, uint32_t op, uint8_t *block) {
  uint8_t* packet = malloc(HEADER_LEN + JBOD_BLOCK_SIZE); // Packet is malloced to provide space for the read operation to execute.
  op = htonl(op);  // op is converted to a netbyte.
  
  memcpy(packet,&op,4);
  uint8_t infoCode = 0; // Initialization of the infocode.
  bool blockExists = false; // Initialization of boolean to determine if block needs to be sent.

  if (block != NULL) { // If statement checks if write operation is being conducted, and initializes variables accordingly.
    blockExists = true;
    infoCode = 2;
  }


  bool checker; // Checker determines what is returned at the end of the operation.
  memcpy(&packet[4],&infoCode,1);
  
  if (blockExists) {
    memcpy(&packet[5],block,JBOD_BLOCK_SIZE);
    checker = nwrite(sd,HEADER_LEN + JBOD_BLOCK_SIZE,packet);  // Write operation is conducted on the packet with data.
  }
  else {
    checker = nwrite(sd,HEADER_LEN,packet);  // Write operation is conducted on the packet without data.
  }
  free(packet); // Packet is freed before return.

  return checker;
}



/* attempts to connect to server and set the global cli_sd variable to the
 * socket; returns true if successful and false if not. 
 * this function will be invoked by tester to connect to the server at given ip and port.
 * you will not call it in mdadm.c
*/
bool jbod_connect(const char *ip, uint16_t port) {
  /* Function as a whole connects to the server by creating a socket and binding it to the server. */
  struct sockaddr_in caddr;

  caddr.sin_family = AF_INET;
  caddr.sin_port = htons(port);
  
  if (inet_aton(ip, &caddr.sin_addr) == 0) {
    return false;
  }

  cli_sd = socket(PF_INET, SOCK_STREAM, 0);
  if (cli_sd == -1) {
    printf("Error on socket creation [%s]\n", strerror(errno));
    return false;
  }

  if (connect(cli_sd, (const struct sockaddr *)&caddr, sizeof(caddr)) == -1) {
    printf("Error on socket connect [%s]\n", strerror(errno));
    return false;
  }
  return true;
}




/* disconnects from the server and resets cli_sd */
void jbod_disconnect(void) {
  close(cli_sd);
  cli_sd = -1;
}



/* sends the JBOD operation to the server (use the send_packet function) and receives 
(use the recv_packet function) and processes the response. 

The meaning of each parameter is the same as in the original jbod_operation function. 
return: 0 means success, -1 means failure.
*/
int jbod_client_operation(uint32_t op, uint8_t *block) {
  uint32_t* rop = malloc(4); // Variable to store returned op code.
  uint8_t* rret = malloc(1); // Variable to store returned ret value.
  uint8_t signifier; // Signifier is initialized, and will store last bit of rret value.

  bool returned = send_packet(cli_sd,op,block); // Packet is sent.

  if (returned == false) { // Check is conducted to determine if packet was sent.
    return -1;
  }

  returned = recv_packet(cli_sd,rop,rret,block); // Packet is received.

  if (returned == false) { // Check is conducted to determine if the packet was received.
    return -1;
  }

  signifier = ((*rret << 7) >> 7); // Bit manipulation is conducted to isolate rightmost bit.
  free(rop); // Value is freed as it isn't useful anymore.
  free(rret); // Value is freed as it isn't useful anymore.

  if (signifier == (uint8_t) 1) { // Signifier determines return value.
    return -1;
  } else {
    return 0;
  }
}
