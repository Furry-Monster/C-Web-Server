/**
 * webserver.c -- A webserver written in C
 *
 * Test with curl (if you don't have it, install it):
 *
 *    curl -D - http://localhost:3490/
 *    curl -D - http://localhost:3490/d20
 *    curl -D - http://localhost:3490/date
 *
 * You can also test the above URLs in your browser! They should work!
 *
 * Posting Data:
 *
 *    curl -D - -X POST -H 'Content-Type: text/plain' -d 'Hello, sample data!'
 * http://localhost:3490/save
 *
 * (Posting data is harder to test from a browser.)
 */

#include "cache.h"
#include "file.h"
#include "mime.h"
#include "net.h"
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#define PORT "3490" // the port users will be connecting to

#define SERVER_FILES "./serverfiles"
#define SERVER_ROOT "./serverroot"

/**
 * Send an HTTP response
 *
 * header:       "HTTP/1.1 404 NOT FOUND" or "HTTP/1.1 200 OK", etc.
 * content_type: "text/plain", etc.
 * body:         the data to send.
 *
 * Return the value from the send() function.
 */
int send_response(int fd, char *header, char *content_type, void *body,
                  int content_length) {
  const int max_response_size = 262144;
  const int time_str_size = 40;
  char response[max_response_size];
  char date[time_str_size];

  memset(response, 0, max_response_size);
  memset(date, 0, time_str_size);

  // Load time info
  time_t rawtime = time(NULL);
  struct tm *tp = localtime(&rawtime);
  strftime(date, sizeof(date), "Date: %a %b %d %H:%M:%S %Z %Y", tp);

  // Build HTTP response and store it in response
  int response_length = snprintf(response, max_response_size,
                                 "%s\n"
                                 "Date: %s\n"
                                 "Content-Type: %s\n"
                                 "Content-Length: %d\n"
                                 "Connection: close\n"
                                 "\n",
                                 header, date, content_type, content_length);

  // Send it all!
  // Send head first
  int rv = send(fd, response, response_length, 0);

  if (rv < 0) {
    perror("send");
  }

  // then send binary data...
  rv = send(fd, body, content_length, 0);

  if (rv < 0) {
    perror("send");
  }

  return rv;
}

/**
 * Send a /d20 endpoint response
 */
void get_d20(int fd) {
  char data[8];

  // Generate a random number between 1 and 20 inclusive
  srand(time(NULL));
  int randv = rand() % 20 + 1;

  // Use send_response() to send it back as text/plain data
  snprintf(data, 8, "%d", randv);
  send_response(fd, "HTTP/1.1 200 OK", "text/plain", data, strlen(data));
}

/**
 * Send a 404 response
 */
void resp_404(int fd) {
  char filepath[4096];
  struct file_data *filedata;
  char *mime_type;

  // Fetch the 404.html file
  snprintf(filepath, sizeof filepath, "%s/404.html", SERVER_FILES);
  filedata = file_load(filepath);

  if (filedata == NULL) {
    char *ise_str = "Server crushed...";
    send_response(fd, "HTTP/1.1 500 Internal Server Error", "text/plain",
                  ise_str, strlen(ise_str));
    fprintf(stderr, "cannot find system 404 file\n");
  }

  mime_type = mime_type_get(filepath);

  send_response(fd, "HTTP/1.1 404 NOT FOUND", mime_type, filedata->data,
                filedata->size);

  file_free(filedata);
}

/**
 * Send bad request repond
 */
void bad_req_resp(int fd) {
  char *str = "Wtf is this shit request";

  send_response(fd, "HTTP/1.1 400 Bad Request", "text/plain", str, strlen(str));
}

/**
 * Read and return a file from disk or cache
 */
void get_file(int fd, struct cache *cache, char *request_path) {
  char filepath[4096];
  struct file_data *filedata;
  char *mime_type;

  // root should be redirect to index
  if (strcmp(request_path, "/") == 0)
    sprintf(request_path, "%s", "/index.html");

  // Fetch file from root dir, but firstly , let's check cache.
  memset(filepath, 0, 4096);
  snprintf(filepath, sizeof filepath, "%s/%s", SERVER_ROOT, request_path);
  struct cache_entry *entry = cache_get(cache, filepath);
  if (entry != NULL) {
    // if cache hit, send it directly
    send_response(fd, "HTTP/1.1 200 OK", entry->content_type, entry->content,
                  entry->content_length);
    return;
  }

  filedata = file_load(filepath);

  // if not found , respond 404 , and end this function
  if (filedata == NULL) {
    resp_404(fd);
    return;
  }

  mime_type = mime_type_get(filepath);

  send_response(fd, "HTTP/1.1 200 OK", mime_type, filedata->data,
                filedata->size);

  // cache not hit but file accessed, we add it into cache
  cache_put(cache, filepath, mime_type, filedata->data, filedata->size);

  file_free(filedata);
}

/**
 * Search for the end of the HTTP header
 *
 * "Newlines" in HTTP can be \r\n (carriage return followed by newline) or \n
 * (newline) or \r (carriage return).
 */
const char *find_start_of_body(char *request) {
  const char *body_start = strstr(request, "\r\n\r\n");
  if (body_start == NULL)
    return NULL;
  body_start += 4; // skip /r/n/r/n sequence

  size_t bodylen = strlen(body_start);

  // copy to new block
  char *body = malloc(bodylen + 1);
  memset(body, 0, bodylen + 1);
  if (body == NULL) {
    perror("Memory allocate failed");
    return NULL;
  }

  memcpy(body, body_start, bodylen);
  perror(body);
  return body;
}

/**
 * Save to default file "rubbish.txt"
 *
 */
void post_save(int fd, const void *body, struct cache *cache,
               char *request_path) {
  char filepath[4096];
  struct file_data *filedata;
  char *mime = "application/json";
  char *resp_body = "{\"status\":\"ok\"}";

  // find the server root path first.
  memset(filepath, 0, 4096);
  snprintf(filepath, sizeof filepath, "%s/%s", SERVER_ROOT, request_path);

  // modify file
  filedata = file_load(filepath);
  if (filedata == NULL) {
    bad_req_resp(fd);
    return;
  }
  file_modify(filedata, body);

  // try to save the modify buffering files
  if (!file_save(filedata)) {
    file_free(filedata);
    return;
  }

  // remember to update cache
  struct cache_entry *entry = cache_get(cache, request_path);
  if (entry != NULL) {
    // set dirty
    entry->dirty = 1;
  }

  send_response(fd, "HTTP/1.1 200 OK", mime, resp_body, strlen(resp_body));

  file_free(filedata);
}

/**
 * Handle HTTP request and send response
 */
void handle_http_request(int fd, struct cache *cache) {
  const int request_buffer_size = 65536; // 64K
  const int oprlen = 16;
  const int pathlen = 256;
  char request[request_buffer_size];
  char opr[oprlen];
  char path[pathlen];

  memset(request, 0, request_buffer_size);
  memset(opr, 0, oprlen);
  memset(path, 0, pathlen);

  // Read request
  int bytes_recvd = recv(fd, request, request_buffer_size - 1, 0);

  if (bytes_recvd < 0) {
    perror("recv");
    return;
  }

  // Read the first two components of the first line of the request
  int nread = sscanf(request, "%s %s", opr, path);
  if (nread == 0)
    return;

  // If GET, handle the get endpoints
  if (strcmp(opr, "GET") == 0) {
    if (strcmp(path, "/d20") == 0)
      get_d20(fd);
    // Otherwise serve the requested file by calling get_file()
    else
      get_file(fd, cache, path);
  }
  // (Stretch) If POST, handle the post request
  else if (strcmp(opr, "POST") == 0) {
    post_save(fd, find_start_of_body(request), cache, path);
  } else {
    resp_404(fd);
  }
}

/**
 * Main
 */
int main(void) {
  int newfd; // listen on sock_fd, new connection on newfd
  struct sockaddr_storage their_addr; // connector's address information
  char s[INET6_ADDRSTRLEN];

  struct cache *cache = cache_create(10, 0);

  // Get a listening socket
  int listenfd = get_listener_socket(PORT);

  if (listenfd < 0) {
    fprintf(stderr, "webserver: fatal error getting listening socket\n");
    exit(1);
  }

  printf("webserver: waiting for connections on port %s...\n", PORT);

  // This is the main loop that accepts incoming connections and
  // responds to the request. The main parent process
  // then goes back to waiting for new connections.

  while (1) {
    socklen_t sin_size = sizeof their_addr;

    // Parent process will block on the accept() call until someone
    // makes a new connection:
    newfd = accept(listenfd, (struct sockaddr *)&their_addr, &sin_size);
    if (newfd == -1) {
      perror("accept");
      continue;
    }

    // Print out a message that we got the connection
    inet_ntop(their_addr.ss_family, get_in_addr((struct sockaddr *)&their_addr),
              s, sizeof s);
    printf("server: got connection from %s\n", s);

    // newfd is a new socket descriptor for the new connection.
    // listenfd is still listening for new connections.

    handle_http_request(newfd, cache);

    close(newfd);
  }

  // Unreachable code

  return 0;
}
