/*
 * SPDX-FileCopyrightText: 2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */
/* HTTP File Server Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <stdio.h>
#include <string.h>
#include <sys/param.h>
#include <sys/unistd.h>
#include <sys/stat.h>
#include <dirent.h>

#include "esp_err.h"
#include "esp_log.h"

#include "esp_vfs.h"
#include "esp_spiffs.h"
#include "esp_http_server.h"
#include "cJSON.h"
#include "protocol_examples_utils.h"
#include "config_db.h"
#include "wifi_intf.h"
#include "display.h"
#include "mount_sdcard.h"

/* Max length a file path can have on storage */
#ifdef CONFIG_FATFS_MAX_LFN
#define FILE_PATH_MAX CONFIG_FATFS_MAX_LFN
#else
#define FILE_PATH_MAX (ESP_VFS_PATH_MAX + CONFIG_SPIFFS_OBJ_NAME_LEN)
#endif

/* Scratch buffer size */
#define SCRATCH_BUFSIZE  8192
#define SCRATCH_NUMBER 3

#define USE_ASYNC_READ 1
#define ASYNC_READ_FILE_THRESHOLD (1024 * 1024)

#define MKDIR_MASK 0744

struct file_server_data {
    /* Base path of file storage */
    char base_path[ESP_VFS_PATH_MAX + 1];

    /* static web files base path */
    char static_files_path[ESP_VFS_PATH_MAX + 1];
    
    /* Scratch buffer for temporary storage during file transfer */
    char scratch[SCRATCH_BUFSIZE * SCRATCH_NUMBER];
};

typedef struct {
    FILE* fp;
    char* req_buf;
    size_t buf_size;
    bool is_read;
    esp_err_t err;
} io_req_item_t;

typedef io_req_item_t io_resp_item_t;

typedef struct {
    QueueHandle_t req_que;
    QueueHandle_t resp_que;
    TaskHandle_t task_handle;
} io_thread_param_t;

static const char *TAG = "file_server";

static void free_io_thread_param(io_thread_param_t* param) {
    if (param) {
        if (param->req_que) {
            vQueueDelete(param->req_que);
        }
        if (param->resp_que) {
            vQueueDelete(param->resp_que);
        }
        free(param);
    }
}

static void io_thread_task_func(void* param)
{
    io_thread_param_t* io_param = (io_thread_param_t*)param;

    while (1) {
        io_req_item_t req_item;
        if (xQueueReceive(io_param->req_que, &req_item, portMAX_DELAY) != pdTRUE) {
            ESP_LOGW(TAG, "Receive request item timeout");
            continue;
        }

        // special signal
        if (req_item.req_buf == NULL) {
            TaskHandle_t task = io_param->task_handle;
            free_io_thread_param(io_param);
            fclose(req_item.fp);
            vTaskDelete(task);
            break;
        }

        io_resp_item_t resp_item = req_item;
        if (req_item.is_read) {
            int read_size = fread(resp_item.req_buf, 1, req_item.buf_size, req_item.fp);
            if (read_size <= 0) {
                resp_item.buf_size = 0;
                resp_item.err = ESP_FAIL;
            } else {
                resp_item.buf_size = read_size;
                resp_item.err = ESP_OK;
            }
        } else {
            int write_size = fwrite(req_item.req_buf, 1, req_item.buf_size, req_item.fp);
            if (write_size != req_item.buf_size) {
                resp_item.err = ESP_FAIL;
            } else {
                resp_item.err = ESP_OK;
            }
        }

        if (xQueueSend(io_param->resp_que, &resp_item, portMAX_DELAY) != pdTRUE) {
            // data will be lost here
            ESP_LOGE(TAG, "Failed to send response item");
        }
    }
}

static esp_err_t create_async_io_thread(io_thread_param_t** param) {
    // Add 1 for the last special request item
    QueueHandle_t req_que = xQueueCreate(SCRATCH_NUMBER+1, sizeof(io_req_item_t));
    if (req_que == NULL) {
        return ESP_ERR_NO_MEM;
    }

    QueueHandle_t resp_que = xQueueCreate(SCRATCH_NUMBER, sizeof(io_resp_item_t));
    if (resp_que == NULL) {
        vQueueDelete(req_que);
        return ESP_ERR_NO_MEM;
    }

    io_thread_param_t *io_param = (io_thread_param_t *)malloc(sizeof(io_thread_param_t));
    if (io_param == NULL) {
        vQueueDelete(req_que);
        vQueueDelete(resp_que);
        return ESP_ERR_NO_MEM;
    }

    io_param->req_que = req_que;
    io_param->resp_que = resp_que;
    io_param->task_handle = NULL;

    int res = xTaskCreate(io_thread_task_func, "http_io_thread", 4096, io_param, tskIDLE_PRIORITY+5, &io_param->task_handle);
    if (res != pdPASS) {
        ESP_LOGE(TAG, "Failed to create task for http io thread.");
        free_io_thread_param(io_param);
        return ESP_ERR_NO_MEM;
    }

    *param = io_param;
    return ESP_OK;
}

/* Send HTTP response with a run-time generated html consisting of
 * a list of all files and folders under the requested path.
 * In case of SPIFFS this returns empty list when path is any
 * string other than '/', since SPIFFS doesn't support directories */
static esp_err_t http_resp_dir_json(httpd_req_t *req, const char *dirpath)
{
    char entrypath[FILE_PATH_MAX];
    char entrysize[16];
    const char *entrytype;

    struct dirent *entry;
    struct stat entry_stat;

    DIR *dir = opendir(dirpath);
    const size_t dirpath_len = strlen(dirpath);

    /* Retrieve the base path of file storage to construct the full path */
    strlcpy(entrypath, dirpath, sizeof(entrypath));

    if (!dir) {
        ESP_LOGE(TAG, "Failed to stat dir : %s", dirpath);
        /* Respond with 404 Not Found */
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Directory does not exist");
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr_chunk(req, "[\n");

    bool first_entry = true;

    /* Iterate over all files / folders and fetch their names and sizes */
    while ((entry = readdir(dir)) != NULL) {
        entrytype = (entry->d_type == DT_DIR ? "directory" : "file");

        strlcpy(entrypath + dirpath_len, entry->d_name, sizeof(entrypath) - dirpath_len);
        if (stat(entrypath, &entry_stat) == -1) {
            ESP_LOGE(TAG, "Failed to stat %s : %s", entrytype, entry->d_name);
            continue;
        }
        sprintf(entrysize, "%ld", entry_stat.st_size);
        ESP_LOGI(TAG, "Found %s : %s (%s bytes)", entrytype, entry->d_name, entrysize);

        if (first_entry) {
            httpd_resp_sendstr_chunk(req, "{\n");
            first_entry = false;
        } else {
            httpd_resp_sendstr_chunk(req, ",\n{\n");
        }
        httpd_resp_sendstr_chunk(req, "\"name\":\"");
        httpd_resp_sendstr_chunk(req, entry->d_name);
        if (entry->d_type == DT_DIR) {
            // add a path seperator at the end of directories
            httpd_resp_sendstr_chunk(req, "/\",\n");
        } else {
            httpd_resp_sendstr_chunk(req, "\",\n");
        }

        httpd_resp_sendstr_chunk(req, "\"mtime\":\"");
        const char *time_str = ctime(&entry_stat.st_mtime);
        if (time_str && strlen(time_str) > 1) {
            httpd_resp_send_chunk(req, time_str, strlen(time_str) - 1);
        } else {
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to read mtime");
            return ESP_FAIL;
        }
        httpd_resp_sendstr_chunk(req, "\",\n");

        httpd_resp_sendstr_chunk(req, "\"size\":");
        httpd_resp_sendstr_chunk(req, entrysize);
        httpd_resp_sendstr_chunk(req, "\n}");
    }
    closedir(dir);

    /* Finish the file list table */
    httpd_resp_sendstr_chunk(req, "]");

    /* Send empty chunk to signal HTTP response completion */
    httpd_resp_sendstr_chunk(req, NULL);
    return ESP_OK;
}

#define IS_FILE_EXT(filename, ext) \
    (strcasecmp(&filename[strlen(filename) - sizeof(ext) + 1], ext) == 0)

/* Set HTTP response content type according to file extension */
static esp_err_t set_content_type_from_file(httpd_req_t *req, const char *filename)
{
    if (IS_FILE_EXT(filename, ".pdf")) {
        return httpd_resp_set_type(req, "application/pdf");
    } else if (IS_FILE_EXT(filename, ".html")) {
        return httpd_resp_set_type(req, "text/html");
    } else if (IS_FILE_EXT(filename, ".jpeg")) {
        return httpd_resp_set_type(req, "image/jpeg");
    } else if (IS_FILE_EXT(filename, ".ico")) {
        return httpd_resp_set_type(req, "image/x-icon");
    } else if (IS_FILE_EXT(filename, ".css")) {
        return httpd_resp_set_type(req, "text/css");
    } else if (IS_FILE_EXT(filename, ".js")) {
        return httpd_resp_set_type(req, "text/javascript");
    } else if (IS_FILE_EXT(filename, ".json")) {
        return httpd_resp_set_type(req, "application/json");
    }
    /* This is a limited set only */
    /* For any other type always set as plain text */
    return httpd_resp_set_type(req, "text/plain");
}

static esp_err_t set_content_length(httpd_req_t *req, size_t len)
{
    static char content_len_str[16];
    sprintf(content_len_str, "%d", len);
    return httpd_resp_set_hdr(req, "Content-Length", content_len_str);
}

static esp_err_t get_storage_file_name(char* dest, size_t dest_size, const char *base_path, const char *encoded_name)
{
    (void)strncpy(dest, base_path, dest_size);

    int src_len = strlen(base_path);
    if (src_len < dest_size) {
        dest += src_len;
        dest_size -= src_len;
    } else {
        return ESP_ERR_NO_MEM;
    }

    int encoded_len = strlen(encoded_name);
    // be conservative in case no special character is in the source
    if (dest_size <= encoded_len) {
        return ESP_ERR_NO_MEM;
    }

    // example_uri_decode return no info about how long is the decoded string.
    // bzero the destination buffer first.
    bzero(dest, dest_size);
    example_uri_decode(dest, encoded_name, encoded_len);

    return ESP_OK;
}

static esp_err_t send_buf_to_io_thread(io_thread_param_t* io_param, FILE* fp, char* buf, size_t buf_len, bool is_read)
{
    io_req_item_t req_item = {
        .fp = fp,
        .req_buf = buf,
        .buf_size = buf_len,
        .is_read = is_read,
        .err = ESP_OK
    };
                
    if (xQueueSend(io_param->req_que, &req_item, portMAX_DELAY) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to queue an request item");
        return ESP_FAIL;
    }

    return ESP_OK;
}

static esp_err_t init_async_io_thread(struct file_server_data* server_data, io_thread_param_t** io_param, FILE* fp, bool is_read)
{
    esp_err_t ret = create_async_io_thread(io_param);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Not enough resource to create async io thread");
        // switch to read synchronously
        return ESP_OK;
    } else {
        // send buffers to the async thread
        for (int i = 0; i < SCRATCH_NUMBER; i++) {
            ret = send_buf_to_io_thread(*io_param, fp, &(server_data->scratch[SCRATCH_BUFSIZE * i]), SCRATCH_BUFSIZE, is_read);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "Failed to queue buffers to async io thread");
                return ret;
            }
        }
    }

    return ESP_OK;
}

static esp_err_t read_from_io_thread(io_thread_param_t* io_param, char** buf, size_t* buf_size)
{
    io_resp_item_t resp_item;
    if (xQueueReceive(io_param->resp_que, &resp_item, portMAX_DELAY) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to recieve an request item from queue");
        return ESP_FAIL;
    }

    if (resp_item.is_read != true) {
        ESP_LOGE(TAG, "unexpected response from queue");
        return ESP_FAIL;
    }

    if (resp_item.err == ESP_OK) {
        *buf = resp_item.req_buf;
        *buf_size = resp_item.buf_size;
    } else {
        *buf = resp_item.req_buf;
        *buf_size = 0;
    }
    return ESP_OK;
}

/* Handler to download a file kept on the server */
static esp_err_t get_file_from_storage(httpd_req_t *req, const char* uri_prefix, const char* base_path, bool handle_dir, bool display_status)
{
    char filename[FILE_PATH_MAX+1];
    FILE *fd = NULL;
    struct stat file_stat;

    const char* encoded_path = req->uri + strlen(uri_prefix);
    esp_err_t ret = get_storage_file_name(filename, sizeof(filename), base_path, encoded_path);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "filename is too long, len=%u", strlen(encoded_path));
        /* Respond with 500 Internal Server Error */
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Filename too long");
        return ESP_FAIL;
    }

    /* If name has trailing '/', respond with directory contents */
    if (filename[strlen(filename) - 1] == '/') {
        if (handle_dir) {
            return http_resp_dir_json(req, filename);
        } else if (strcmp(&filename[strlen(base_path)], "/") == 0) {
            if (strlcat(filename, "index.html", sizeof(filename)) >= sizeof(filename)) {
                ESP_LOGE(TAG, "filename is too long");
                /* Respond with 500 Internal Server Error */
                httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Not enough space for filename");
                return ESP_FAIL;
            }
        } else {
            ESP_LOGE(TAG, "can't return dir: %s", filename);
            /* Respond with 404 Not Found */
            httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "File does not exist");
            return ESP_FAIL;            
        }
    }

    if (stat(filename, &file_stat) == -1) {
        ESP_LOGE(TAG, "Failed to stat file : %s", filename);
        /* Respond with 404 Not Found */
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "File does not exist");
        return ESP_FAIL;
    }

    fd = fopen(filename, "r");
    if (!fd) {
        ESP_LOGE(TAG, "Failed to read existing file : %s", filename);
        /* Respond with 500 Internal Server Error */
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to read existing file");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Sending file : %s (%ld bytes)...", filename, file_stat.st_size);
    set_content_type_from_file(req, filename);
    ret = set_content_length(req, file_stat.st_size);
    if (ret != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to set content length");
        fclose(fd);
        return ESP_FAIL;
    }

    // from now no we should use goto free_resources to free resources
    io_thread_param_t* io_param = NULL;
#if USE_ASYNC_READ
    if (file_stat.st_size >= ASYNC_READ_FILE_THRESHOLD) {
        ret = init_async_io_thread((struct file_server_data *)req->user_ctx, &io_param, fd, true);
        if (ret != ESP_OK) {
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "internal error");
            goto free_resources;
        }
    }
#endif
    if (display_status) {
        update_file_server_status(filename, FILE_OPERATION_DOWNLOAD);
    }

    /* Retrieve the pointer to scratch buffer for temporary storage */
    size_t chunksize;
    do {
        /* Read file in chunks into the scratch buffer */
        char* chunk = NULL;
        if (io_param) {
            ret = read_from_io_thread(io_param, &chunk, &chunksize);
            if (ret != ESP_OK) {
                /* Abort sending file */
                httpd_resp_sendstr_chunk(req, NULL);
                /* Respond with 500 Internal Server Error */
                httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to read file");
                goto free_resources;
            }
        } else {
            chunk = ((struct file_server_data *)req->user_ctx)->scratch;
            chunksize = fread(chunk, 1, SCRATCH_BUFSIZE, fd);
        }
        if (chunksize > 0) {
            /* Send the buffer contents as HTTP response chunk */
            ret = httpd_resp_send_chunk(req, chunk, chunksize);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "File sending failed!");
                /* Abort sending file */
                httpd_resp_sendstr_chunk(req, NULL);
                /* Respond with 500 Internal Server Error */
                httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to send file");
                goto free_resources;
            }

            if (io_param != NULL) {
                ret = send_buf_to_io_thread(io_param, fd, chunk, SCRATCH_BUFSIZE, true);
                if (ret != ESP_OK) {
                    ESP_LOGE(TAG, "Failed to requeue read buffer");
                    httpd_resp_sendstr_chunk(req, NULL);
                    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "internal error");
                    goto free_resources;
                }
            }
        }

        /* Keep looping till the whole file is sent */
    } while (chunksize != 0);

free_resources:
    // let io_thread close fd otherwise there could be race issue on fd
    if (io_param) {
        esp_err_t ret2 = send_buf_to_io_thread(io_param, fd, NULL, 0, true);
        if (ret2 != ESP_OK) {
            ESP_LOGE(TAG, "Failed to requeue read buffer");
        }
    } else {
        fclose(fd);
    }
    if (display_status) {
        update_file_server_status(NULL, FILE_OPERATION_IDLE);
    }

    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "File sending complete");

        /* Respond with an empty chunk to signal HTTP response completion */
#ifdef CONFIG_EXAMPLE_HTTPD_CONN_CLOSE_HEADER
        httpd_resp_set_hdr(req, "Connection", "close");
#endif
        httpd_resp_send_chunk(req, NULL, 0);
    }
    return ret;
}

/* Handler to download a file kept on the server */
static esp_err_t download_get_handler(httpd_req_t *req)
{
    const char* uri_prefix = "/get";
    const char* base_path = ((struct file_server_data *)req->user_ctx)->base_path;

    if (!is_sdcard_mounted()) {
        ESP_LOGE(TAG, "sdcard is not mounted");
        /* Respond with 500 Internal Server Error */
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "sdcard unmounted");
        return ESP_FAIL;
    }

    return get_file_from_storage(req, uri_prefix, base_path, true, true);
}

static esp_err_t static_files_get_handler(httpd_req_t *req)
{
    const char* uri_prefix = "";
    const char* base_path = ((struct file_server_data *)req->user_ctx)->static_files_path;

    return get_file_from_storage(req, uri_prefix, base_path, false, false);
}

static esp_err_t upload_file(httpd_req_t *req, const char* file_path)
{
    FILE *fd = fopen(file_path, "w");
    if (!fd) {
        ESP_LOGE(TAG, "Failed to create file : %s", file_path);
        /* Respond with 500 Internal Server Error */
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to create file");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Receiving file : %s...", file_path);
    update_file_server_status(file_path, FILE_OPERATION_UPLOAD);

    /* Retrieve the pointer to scratch buffer for temporary storage */
    char *buf = ((struct file_server_data *)req->user_ctx)->scratch;
    int received;

    /* Content length of the request gives
     * the size of the file being uploaded */
    int remaining = req->content_len;

    while (remaining > 0) {

        ESP_LOGI(TAG, "Remaining size : %d", remaining);
        /* Receive the file part by part into a buffer */
        if ((received = httpd_req_recv(req, buf, MIN(remaining, SCRATCH_BUFSIZE))) <= 0) {
            if (received == HTTPD_SOCK_ERR_TIMEOUT) {
                /* Retry if timeout occurred */
                continue;
            }

            /* In case of unrecoverable error,
             * close and delete the unfinished file*/
            fclose(fd);
            unlink(file_path);

            ESP_LOGE(TAG, "File reception failed!");
            /* Respond with 500 Internal Server Error */
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to receive file");
            update_file_server_status(NULL, FILE_OPERATION_IDLE);
            return ESP_FAIL;
        }

        /* Write buffer content to file on storage */
        if (received && (received != fwrite(buf, 1, received, fd))) {
            /* Couldn't write everything to file!
             * Storage may be full? */
            fclose(fd);
            unlink(file_path);

            ESP_LOGE(TAG, "File write failed!");
            /* Respond with 500 Internal Server Error */
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to write file to storage");
            update_file_server_status(NULL, FILE_OPERATION_IDLE);
            return ESP_FAIL;
        }

        /* Keep track of remaining size of
         * the file left to be uploaded */
        remaining -= received;
    }

    /* Close file upon upload completion */
    fclose(fd);
    ESP_LOGI(TAG, "File reception complete");
    update_file_server_status(NULL, FILE_OPERATION_IDLE);

#ifdef CONFIG_EXAMPLE_HTTPD_CONN_CLOSE_HEADER
    httpd_resp_set_hdr(req, "Connection", "close");
#endif
    httpd_resp_sendstr(req, "File uploaded successfully");
    return ESP_OK;
}

static esp_err_t make_directory(httpd_req_t *req, const char* abs_path)
{
    int ret = mkdir(abs_path, MKDIR_MASK);
    if (ret != 0) {
        ESP_LOGE(TAG, "Failed to create directory : %s", abs_path);
        /* Respond with 500 Internal Server Error */
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to create directory");
        return ESP_FAIL;
    }

#ifdef CONFIG_EXAMPLE_HTTPD_CONN_CLOSE_HEADER
    httpd_resp_set_hdr(req, "Connection", "close");
#endif
    httpd_resp_sendstr(req, "Directory created successfully");
    return ESP_OK;
}

/* Handler to upload a file onto the server */
static esp_err_t upload_post_handler(httpd_req_t *req)
{
    char file_name[FILE_PATH_MAX+1];
    struct stat file_stat;

    if (!is_sdcard_mounted()) {
        ESP_LOGE(TAG, "sdcard is not mounted");
        /* Respond with 500 Internal Server Error */
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "sdcard unmounted");
        return ESP_FAIL;
    }

    /* Skip leading "/upload" from URI to get filename */
    /* Note sizeof() counts NULL termination hence the -1 */
    const char* encoded_path = req->uri + strlen("/upload");
    const char* base_path = ((struct file_server_data *)req->user_ctx)->base_path;

    esp_err_t ret = get_storage_file_name(file_name, sizeof(file_name), base_path, encoded_path);
    if (ret != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "can't parse filename");
        return ESP_FAIL;
    }

    /* Filename cannot have a trailing '/' */
    bool is_dir = file_name[strlen(file_name) - 1] == '/';

    if (stat(file_name, &file_stat) == 0) {
        ESP_LOGE(TAG, "File already exists : %s", file_name);
        /* Respond with 400 Bad Request */
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "File already exists");
        return ESP_FAIL;
    }

    if (is_dir) {
        return make_directory(req, file_name);
    } else {
        return upload_file(req, file_name);
    }
}

/* Handler to delete a file from the server */
static esp_err_t delete_post_handler(httpd_req_t *req)
{
    char filename[FILE_PATH_MAX+1];
    struct stat file_stat;

    if (!is_sdcard_mounted()) {
        ESP_LOGE(TAG, "sdcard is not mounted");
        /* Respond with 500 Internal Server Error */
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "sdcard unmounted");
        return ESP_FAIL;
    }
    
    const char* encoded_path = req->uri + strlen("/delete");
    const char* base_path = ((struct file_server_data *)req->user_ctx)->base_path;

    esp_err_t ret = get_storage_file_name(filename, sizeof(filename), base_path, encoded_path);
    if (ret != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "can't parse filename");
        return ESP_FAIL;
    }

    bool is_dir = filename[strlen(filename) - 1] == '/';

    if (stat(filename, &file_stat) == -1) {
        ESP_LOGE(TAG, "File does not exist : %s", filename);
        /* Respond with 400 Bad Request */
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "File does not exist");
        return ESP_FAIL;
    }

    int res = 0;
    if (!is_dir) {
        ESP_LOGI(TAG, "Deleting file : %s", filename);
        /* Delete file */
        res = unlink(filename);
    } else {
        res = rmdir(filename);
    }

    if (res != 0) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "can't delete entry");
        return ESP_FAIL;
    }

#ifdef CONFIG_EXAMPLE_HTTPD_CONN_CLOSE_HEADER
    httpd_resp_set_hdr(req, "Connection", "close");
#endif
    httpd_resp_sendstr(req, "File deleted successfully");
    return ESP_OK;
}

const char *wifi_mode_to_str(wifi_mode_t mode)
{
    switch (mode) {
    case WIFI_MODE_NULL:
        return "NULL";
    case WIFI_MODE_STA:
        return "STA";
    case WIFI_MODE_AP:
        return "AP";
    default:
        return "UNKNOWN";
    }
}

wifi_mode_t str_to_wifi_mode(const char* str)
{
    if (strcmp(str, "NULL") == 0) {
        return WIFI_MODE_NULL;
    } else if (strcmp(str, "STA") == 0) {
        return WIFI_MODE_STA;
    } else if (strcmp(str, "AP") == 0) {
        return WIFI_MODE_AP;
    } else {
        return WIFI_MODE_NULL;
    }
}

static esp_err_t send_wifi_config_json(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr_chunk(req, "{\n");

    wifi_mode_t mode = query_wifi_mode();
    httpd_resp_sendstr_chunk(req, "\"mode\": \"");
    httpd_resp_sendstr_chunk(req, wifi_mode_to_str(mode));
    httpd_resp_sendstr_chunk(req, "\",\n");

    char ssid[WIFI_SSID_MAX_LEN];
    esp_err_t ret = query_wifi_ssid(ssid, WIFI_SSID_MAX_LEN);
    if (ret != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to read WiFi SSID from config");
        return ESP_FAIL;
    }
    
    httpd_resp_sendstr_chunk(req, "\"ssid\": \"");
    httpd_resp_sendstr_chunk(req, ssid);
    httpd_resp_sendstr_chunk(req, "\",\n");

    // can't return real passwd
    httpd_resp_sendstr_chunk(req, "\"passwd\": \"*\"\n");

    httpd_resp_sendstr_chunk(req, "}");
    httpd_resp_sendstr_chunk(req, NULL);
    return ESP_OK;
}

static esp_err_t device_config_get_handler(httpd_req_t *req)
{
    const char* prefix = "/device/";
    const char* last_token = req->uri + strlen(prefix);

    if (strcasecmp(last_token, "wifi") == 0) {
        return send_wifi_config_json(req);
    } else {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Device config not found");
        return ESP_FAIL;
    }
}

static esp_err_t parse_http_req(httpd_req_t* req, cJSON** root)
{
    size_t buf_len = req->content_len;
    char* body = (char*)malloc(buf_len);
    if (!body) {
        return ESP_ERR_NO_MEM;
    }

    int ret = httpd_req_recv(req, body, buf_len);
    if (ret <= 0) {
        free(body);
        return ESP_ERR_INVALID_ARG;
    }

    *root = cJSON_ParseWithLength(body, buf_len);
    free(body);
    if (!root) {
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}

struct wifi_config_param_t {
    wifi_mode_t mode;
    char ssid[WIFI_SSID_MAX_LEN];
    char passwd[WIFI_PASSWD_MAX_LEN];
};

static void wifi_update_task(void* param)
{
    struct wifi_config_param_t* wifi_param = (struct wifi_config_param_t*)param;

    // let http server process request first
    vTaskDelay(pdMS_TO_TICKS(1000));

    esp_err_t ret = wifi_update(wifi_param->mode, wifi_param->ssid, wifi_param->passwd);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to update WiFi config");
        goto free_param;
    }

    ret = save_wifi_mode(wifi_param->mode);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save WiFi mode");
        goto free_param;
    }

    ret = save_wifi_ssid(wifi_param->ssid);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save WiFi SSID");
        goto free_param;
    }

    ret = save_wifi_passwd(wifi_param->passwd);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save WiFi password");
        goto free_param;
    }

free_param:
    free(wifi_param);
}

static esp_err_t update_wifi_config(wifi_mode_t mode, const char* ssid, const char* passwd)
{
    struct wifi_config_param_t* wifi_param = (struct wifi_config_param_t*)malloc(sizeof(struct wifi_config_param_t));
    if (!wifi_param) {
        return ESP_ERR_NO_MEM;
    }

    wifi_param->mode = mode;
    size_t req_len = strlcpy(wifi_param->ssid, ssid, WIFI_SSID_MAX_LEN);
    if (req_len >= WIFI_SSID_MAX_LEN) {
        free(wifi_param);
        return ESP_ERR_NO_MEM;
    }

    req_len = strlcpy(wifi_param->passwd, passwd, WIFI_PASSWD_MAX_LEN);
    if (req_len >= WIFI_PASSWD_MAX_LEN) {
        free(wifi_param);
        return ESP_ERR_NO_MEM;
    }

    int ret = xTaskCreate(wifi_update_task, "wifi_update_task", 8192, wifi_param, 5, NULL);
    if (ret != pdPASS) {
        free(wifi_param);
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}

static esp_err_t process_wifi_config_json(httpd_req_t *req)
{
    cJSON* root = NULL;
    esp_err_t ret = parse_http_req(req, &root);
    if (ret != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to parse JSON");
        return ESP_FAIL;
    }
    
    cJSON *mode = cJSON_GetObjectItem(root, "mode");
    char* mode_str = cJSON_GetStringValue(mode);
    if (!mode || !mode_str) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing mode");
        ret = ESP_ERR_INVALID_ARG;
        goto err_out;
    }

    cJSON *ssid = cJSON_GetObjectItem(root, "ssid");
    char* ssid_str = cJSON_GetStringValue(ssid);
    if (!ssid) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing ssid");
        ret = ESP_ERR_INVALID_ARG;
        goto err_out;
    }

    cJSON *passwd = cJSON_GetObjectItem(root, "passwd");
    char* passwd_str = cJSON_GetStringValue(passwd);
    if (!passwd) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing passwd");
        ret = ESP_ERR_INVALID_ARG;
        goto err_out;
    }

    wifi_mode_t mode_val = str_to_wifi_mode(mode_str);
    ret = update_wifi_config(mode_val, ssid_str, passwd_str);
    if (ret != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to update WiFi config");
        goto err_out;
    }

err_out:
    if (root) {
        cJSON_Delete(root);
    }
    return ret;
}

static esp_err_t device_config_put_handler(httpd_req_t *req)
{
    const char* prefix = "/device/";
    const char* last_token = req->uri + strlen(prefix);

    if (strcasecmp(last_token, "wifi") == 0) {
        esp_err_t ret = process_wifi_config_json(req);
        if (ret == ESP_OK) {
            httpd_resp_sendstr_chunk(req, "Processing request ...");
            httpd_resp_sendstr_chunk(req, NULL);
        }
        return ret;
    } else {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Device config not found");
        return ESP_FAIL;
    }
}

/* Function to start the file server */
esp_err_t example_start_file_server(const char *base_path, const char *web_path)
{
    static struct file_server_data *server_data = NULL;

    if (server_data) {
        ESP_LOGE(TAG, "File server already started");
        return ESP_ERR_INVALID_STATE;
    }

    /* Allocate memory for server data */
    server_data = calloc(1, sizeof(struct file_server_data));
    if (!server_data) {
        ESP_LOGE(TAG, "Failed to allocate memory for server data");
        return ESP_ERR_NO_MEM;
    }
    strlcpy(server_data->base_path, base_path,
            sizeof(server_data->base_path));
    strlcpy(server_data->static_files_path, web_path,
            sizeof(server_data->static_files_path));

    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();

    /* Use the URI wildcard matching function in order to
     * allow the same handler to respond to multiple different
     * target URIs which match the wildcard scheme */
    config.uri_match_fn = httpd_uri_match_wildcard;

    ESP_LOGI(TAG, "Starting HTTP Server on port: '%d'", config.server_port);
    if (httpd_start(&server, &config) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start file server!");
        return ESP_FAIL;
    }

    /* URI handler for getting uploaded files */
    httpd_uri_t file_download = {
        .uri       = "/get/*",  // Match all URIs of type /path/to/file
        .method    = HTTP_GET,
        .handler   = download_get_handler,
        .user_ctx  = server_data    // Pass server data as context
    };
    httpd_register_uri_handler(server, &file_download);

    /* URI handler for uploading files to server */
    httpd_uri_t file_upload = {
        .uri       = "/upload/*",   // Match all URIs of type /upload/path/to/file
        .method    = HTTP_POST,
        .handler   = upload_post_handler,
        .user_ctx  = server_data    // Pass server data as context
    };
    httpd_register_uri_handler(server, &file_upload);

    /* URI handler for deleting files from server */
    httpd_uri_t file_delete = {
        .uri       = "/delete/*",   // Match all URIs of type /delete/path/to/file
        .method    = HTTP_DELETE,
        .handler   = delete_post_handler,
        .user_ctx  = server_data    // Pass server data as context
    };
    httpd_register_uri_handler(server, &file_delete);

    httpd_uri_t device_config_update = {
        .uri       = "/device/*",
        .method    = HTTP_PUT,
        .handler   = device_config_put_handler,
        .user_ctx  = server_data
    };
    httpd_register_uri_handler(server, &device_config_update);

    httpd_uri_t device_config_query = {
        .uri       = "/device/*",
        .method    = HTTP_GET,
        .handler   = device_config_get_handler,
        .user_ctx  = server_data
    };
    httpd_register_uri_handler(server, &device_config_query);

    /* URI handler for getting uploaded files */
    httpd_uri_t static_page = {
        .uri       = "/*",  // Match all URIs of type /path/to/file
        .method    = HTTP_GET,
        .handler   = static_files_get_handler,
        .user_ctx  = server_data    // Pass server data as context
    };
    httpd_register_uri_handler(server, &static_page);
    
    return ESP_OK;
}
