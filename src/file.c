#include "file.h"
#include <bits/types/struct_iovec.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

/**
 * Loads a file into memory and returns a pointer to the data.
 *
 * Buffer is not NUL-terminated.
 */
struct file_data *file_load(char *filename) {
  char *buffer, *p;
  struct stat buf;
  int bytes_read, bytes_remaining, total_bytes = 0;

  // Get the file size
  if (stat(filename, &buf) == -1) {
    return NULL;
  }

  // Make sure it's a regular file
  if (!(buf.st_mode & S_IFREG)) {
    return NULL;
  }

  // Open the file for reading
  FILE *fp = fopen(filename, "rb");
  if (fp == NULL) {
    return NULL;
  }

  // Allocate that many bytes
  bytes_remaining = buf.st_size;
  p = buffer = malloc(bytes_remaining);

  if (buffer == NULL) {
    fclose(fp);
    return NULL;
  }

  // Read in the entire file
  while (bytes_read = fread(p, 1, bytes_remaining, fp),
         bytes_read != 0 && bytes_remaining > 0) {
    if (bytes_read == -1) {
      free(buffer);
      fclose(fp);
      return NULL;
    }

    bytes_remaining -= bytes_read;
    p += bytes_read;
    total_bytes += bytes_read;
  }

  // Allocate the file data struct
  struct file_data *filedata = malloc(sizeof *filedata);

  if (filedata == NULL) {
    free(buffer);
    fclose(fp);
    return NULL;
  }

  filedata->name = malloc(strlen(filename) + 1);

  if (filedata->name == NULL) {
    free(filedata);
    fclose(fp);
    return NULL;
  }

  memcpy(filedata->name, filename, strlen(filename) + 1);
  filedata->data = buffer;
  filedata->size = total_bytes;

  fclose(fp);
  return filedata;
}

/**
 * Save data to the file
 */
int file_save(struct file_data *filedata) {
  void *buffer, *p;
  struct stat s;
  int bytes_remaining, bytes_write;

  // get file state
  if (stat(filedata->name, &s) == -1)
    return 0;

  // also , make sure it's regular file instead of directory or links
  if (!(s.st_mode & S_IFREG))
    return 0;

  // open up the file
  FILE *fp = fopen(filedata->name, "wb");
  if (fp == NULL) {
    return 0;
  }

  // copy data into buffer
  p = buffer = malloc(filedata->size);
  bytes_remaining = filedata->size;
  if (buffer == NULL) {
    fclose(fp);
    return 0;
  }
  if (memcpy(buffer, filedata->data, filedata->size) == NULL) {
    free(buffer);
    fclose(fp);
    return 0;
  }

  // write buffer into file
  while ((bytes_write = fwrite(p, 1, bytes_remaining, fp)) > 0 &&
         bytes_remaining > 0) {
    if (bytes_write == -1) {
      free(buffer);
      fclose(fp);
      return 0;
    }

    bytes_remaining -= bytes_write;
    p += bytes_write;
  }

  fclose(fp);
  return 1;
}

/**
 * modify data of the giving file structure
 *
 * this function will automatically set the size
 */
int file_modify(struct file_data *filedata, const void *data) {
  void *buffer;
  size_t data_len = strlen(data);

  // allocate buffer
  buffer = malloc(data_len);
  if (buffer == NULL)
    return 0;

  // copy to buffer, and free the old buffer
  memcpy(buffer, data, data_len);
  free(filedata->data);

  // reallocate the filedata
  filedata->data = buffer;
  filedata->size = data_len;

  return 1;
}

/**
 * Frees memory allocated by file_load().
 */
void file_free(struct file_data *filedata) {
  free(filedata->name);
  free(filedata->data);
  free(filedata);
}
