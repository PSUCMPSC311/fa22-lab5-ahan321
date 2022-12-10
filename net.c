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
  int bytes_read = 0;
  
  while (bytes_read < len) {
    int bytes = read(fd, &buf[bytes_read], len-bytes_read);
    if (bytes == -1) {
      return false;
    }
    bytes_read += bytes;
  }
  return true;
}

/* attempts to write n bytes to fd; returns true on success and false on failure 
It may need to call the system call "write" multiple times to reach the size len.
*/
static bool nwrite(int fd, int len, uint8_t *buf) {
  int bytes_written = 0;
  
  while (bytes_written < len) {
    int bytes = write(fd, &buf[bytes_written], len-bytes_written);
    if (bytes == -1) {
      return false;
    }
    bytes_written += bytes;
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
  uint8_t* packet = malloc(261);
  if(!nread(sd,261, packet)) {
    free(packet);
    return false;
  }
  *packet = (uint8_t) ntohs(*packet);

  memcpy(op,&packet[257],4);
  memcpy(ret,&packet[256],1);

  
  
  if ((*ret >> 1) == (uint8_t) 1) {
  memcpy(block,packet,256);
  }

  free(packet);
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
  uint8_t* packet = malloc(261);
  memcpy(&packet[257],&op,4);
  uint8_t infoCode = 0;
  bool blockExists = false;

  if (block != NULL) {
    blockExists = true;
  }
  else {
    infoCode = 2;
  }



  memcpy(&packet[256],&infoCode,1);
  
  if (blockExists) {
    memcpy(packet,block,256);
  }
  
  uint8_t* coded_packet = malloc(261);
  *coded_packet = htons(*packet);
  
  bool checker = nwrite(sd,261,coded_packet);
  free(packet);
  free(coded_packet);

  return checker;
}



/* attempts to connect to server and set the global cli_sd variable to the
 * socket; returns true if successful and false if not. 
 * this function will be invoked by tester to connect to the server at given ip and port.
 * you will not call it in mdadm.c
*/
bool jbod_connect(const char *ip, uint16_t port) {
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
  uint32_t* rop = malloc(4);
  uint8_t* rret = malloc(1);
  uint8_t signifier;

  bool returned = send_packet(cli_sd,op,block);

  if (returned == false) {
    return -1;
  }

  returned = recv_packet(cli_sd,rop,rret,block);

  if (returned == false) {
    return -1;
  }

  signifier = (*rret >> 1);
  free(rop);
  free(rret);

  if (signifier == (uint8_t) 1) {
    return -1;
  } else {
    return 1;
  }
}
