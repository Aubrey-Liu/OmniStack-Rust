#include <cstdio>
#include <iostream>
#include <algorithm>
#include <cstring>
#include <string>
#include <sys/time.h>
#include <map>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>

#include <omnistack/socket/socket.h>
#include <omnistack/common/logger.h>
using namespace omnistack;

#include <CLI/CLI.hpp>

#include <arpa/inet.h>

#define HTTP_STR           "HTTP"
#define HTTPV0_STR         "HTTP/1.0"
#define HTTPV1_STR         "HTTP/1.1"
#define HTTP_GET           "GET"
#define HTTP_POST          "POST"
#define HTTP_CLOSE         "Close"
#define HTTP_KEEP_ALIVE    "Keep-Alive"
#define HOST_HDR           "\nHost:"
#define CONTENT_LENGTH_HDR "\nContent-Length:"
#define CONTENT_TYPE_HDR   "\nContent-Type:"
#define CACHE_CONTROL_HDR  "\nCache-Control:"
#define CONNECTION_HDR     "\nConnection:"
#define DATE_HDR           "\nDate:"
#define EXPIRES_HDR        "\nExpires:"
#define AGE_HDR            "\nAge:"
#define LAST_MODIFIED_HDR	"\nLast-Modified:"
#define IF_MODIFIED_SINCE_HDR	"\nIf-Modified_Since:"
#define PRAGMA_HDR              "\nPragma:"
#define RANGE_HDR               "\nRange:"
#define IF_RANGE_HDR            "\nIf-Range:"
#define ETAG_HDR                "\nETag:"

#define FILE_NAME_LEN 1024
#define MAX_URL_LEN 1024
#define MAX_HOST_LEN 1024
#define HTTP_HEADER_LEN (MAX_URL_LEN + MAX_HOST_LEN + 1024)
#define SPACE_OR_TAB(x)  ((x) == ' '  || (x) == '\t')
#define CR_OR_NEWLINE(x) ((x) == '\r' || (x) == '\n')

struct File {
    char* buffer;
    uint64_t file_size;

    File(): buffer(nullptr), file_size(0) {}
    ~File() {
        if (buffer) {
            delete[] buffer;
            buffer = nullptr;
        }
    }
};

struct HTTPConnection {
    char request[HTTP_HEADER_LEN];
    int request_hdr_len;
    int request_recv;

    char response[HTTP_HEADER_LEN];
    int response_hdr_len;

    uint8_t keep_alive = false;

    std::string file_name = "";
    uint64_t fsize = 0;
    uint64_t sent = 0;

    std::shared_ptr<File> file;
    int16_t fd = 0;
};

static HTTPConnection* transmitting;
static std::map<int, std::shared_ptr<HTTPConnection>> conn;
static std::map<std::string, std::shared_ptr<File>> cache;

static std::shared_ptr<File> cacheFile(const std::string sfname) {
    if (cache[sfname] != nullptr)
        return cache[sfname];
    int fd = open(sfname.c_str(), O_RDONLY);
    if (fd < 0) {
        OMNI_LOG(common::kError) << "Failed to open file " << sfname << "\n";
        return NULL;
    }
    std::shared_ptr<File> file = std::make_shared<File>();
    file->file_size = lseek64(fd, 0, SEEK_END);
    file->buffer = new char[file->file_size];
    if (!file->buffer) {
        close(fd);
        OMNI_LOG(common::kError) << "Failed to create buffer to cache file " << sfname << "\n";
        return nullptr;
    }
    
    uint64_t total_read = 0;
    lseek64(fd, 0, SEEK_SET);
    while (1) {
        ssize_t rb = read(fd, file->buffer + total_read, file->file_size - total_read);
        if (rb < 0) {
            OMNI_LOG(common::kError) << "Failed to read file " << sfname << "\n";
            close(fd);
            return nullptr;
        } else if (rb == 0) {
            OMNI_LOG(common::kInfo) << "File cached " << sfname << " " << total_read << "\n";
            break;
        }
        total_read += rb;
    }
    close(fd);
    return cache[sfname] = file;
}

void WriteAll(int fd, const char* buf, int len) {
    int sent = 0;
    while (sent < len) {
        int ret = socket::bsd::write(fd, (unsigned char*)buf + sent, len - sent);
        if (ret < 0) {
            OMNI_LOG(common::kError) << "Failed to send data\n";
            exit(1);
        }
        sent += ret;
    }
}

static void SendResponse(std::shared_ptr<HTTPConnection> connection) {
    WriteAll(connection->fd, connection->response, connection->response_hdr_len);

    struct timeval tv;
    gettimeofday(&tv, nullptr);
    uint64_t start_tick = tv.tv_sec * 1000000LL + tv.tv_usec;
    WriteAll(connection->fd, connection->file->buffer, connection->file->file_size);

    gettimeofday(&tv, nullptr);
    uint64_t cur_tick = tv.tv_sec * 1000000LL + tv.tv_usec;
    OMNI_LOG(common::kInfo) << ("Successfully send all data\n") << 8.0 * connection->file->file_size / (cur_tick - start_tick) << " Mbps\n";
}

static inline
int find_http_header(char *data, int len) {
	char *temp = data;
	int hdr_len = 0;
	while (!hdr_len && (temp = strchr(temp, '\n')) != NULL) {
		temp++;
		if (*temp == '\n')
			hdr_len = temp - data + 1;
		else if (len > 0 && *temp == '\r' && *(temp + 1) == '\n')
			hdr_len = temp - data + 2;
	}
    data[hdr_len - 1] = '\0';
	return hdr_len;
}

char*
http_get_url(char * data, int data_len, char* value, int value_len)
{
	char *ret = data;
	char *temp;
	int i = 0;

	if (strncmp(data, HTTP_GET, sizeof(HTTP_GET)-1)) {
		*value = 0;
		return NULL;
	}
	
	ret += sizeof(HTTP_GET);
	while (*ret && SPACE_OR_TAB(*ret)) 
		ret++;

	temp = ret;
	while (*temp && *temp != ' ' && i < value_len - 1) {
		value[i++] = *temp++;
	}
	value[i] = 0;
	
	return ret;
}

static char* 
nre_strcasestr(const char* buf, const char* key) {
    int n = strlen(key) - 1;
    const char *p = buf;

	while (*p) {
		while (*p && *p != *key) /* first character match */
			p++;
		if (*p == '\0') 
			return (NULL);
		if (!strncasecmp(p + 1, key + 1, n)) 
			return (char *)p;
		p++;
    }
	return NULL;
}

static inline
char * http_header_str_val(const char* buf, const char *key, const int keylen, char* value, int value_len) {
	char *temp = nre_strcasestr(buf, key);
	int i = 0;
	
	if (temp == NULL) {
		*value = 0;
		return NULL;
	}

	/* skip whitespace or tab */
	temp += keylen;
	while (*temp && SPACE_OR_TAB(*temp))
		temp++;

	/* if we reached the end of the line, forget it */
	if (*temp == '\0' || CR_OR_NEWLINE(*temp)) {
		*value = 0;
		return NULL;
	}

	/* copy value data */
	while (*temp && !CR_OR_NEWLINE(*temp) && i < value_len-1)
		value[i++] = *temp++;
	value[i] = 0;
	
	if (i == 0) {
		*value = 0;
		return NULL;
	}

	return value;
}

static const char *
StatusCodeToString(int scode)
{
	switch (scode) {
		case 200:
			return "OK";
			break;

		case 404:
			return "Not Found";
			break;
	}

	return NULL;
}

static inline
long int http_header_long_val(const char * response, const char* key, int key_len) {
#define C_TYPE_LEN 50
	long int len;
	char value[C_TYPE_LEN];
	char *temp = http_header_str_val(response, key, key_len, value, C_TYPE_LEN);
#undef C_TYPE_LEN

	if (temp == NULL)
		return -1;

	len = strtol(temp, NULL, 10);
	if (errno == EINVAL || errno == ERANGE)
		return -1;

	return len;
}

namespace args {
    std::string dir;
    std::string ip;
    uint16_t port;
    int cpu;
}

int main(int argc, char **argv) {
    srand(time(NULL));
    auto app = new CLI::App();
    app->add_option("-d,--dir", args::dir, "Working Directory")->required();
    app->add_option("-i,--ip", args::ip, "IP addr")->required();
    app->add_option("-p,--port", args::port, "Port")->required();
    app->add_option("-c,--core", args::cpu, "Core to bind")->default_val(-1);

    OMNI_LOG(common::kInfo) << "Working dir: "  << args::dir << std::endl;
    OMNI_LOG(common::kInfo) << "IP: " << args::ip << std::endl;
    OMNI_LOG(common::kInfo) << "Port: " << args::port << std::endl;
    OMNI_LOG(common::kInfo) << "Core: " << args::cpu << std::endl;

    uint32_t host = inet_addr(args::ip.c_str());
    uint16_t sport = htons(args::port);

    int epfd = socket::bsd::epoll_create(1);

    int listen_fd = socket::bsd::socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in listen_addr;
    listen_addr.sin_addr.s_addr = host;
    listen_addr.sin_port = sport;
    listen_addr.sin_family = AF_INET;
    socket::bsd::bind(listen_fd, (struct sockaddr*)&listen_addr, sizeof(listen_addr));
    socket::bsd::listen(listen_fd, 128);

    struct epoll_event evt;
    evt.events = EPOLLIN;
    evt.data.fd = listen_fd;
    socket::bsd::epoll_ctl(epfd, EPOLL_CTL_ADD, listen_fd, &evt);

    struct epoll_event events[32];

    while (true) {
        int nevent = socket::bsd::epoll_wait(epfd, events, 32, 1);
        for (int i = 0; i < nevent; i ++) {
            struct epoll_event* e = events + i;
            if (e->data.fd == listen_fd) {
                // New connection
                int nfd = socket::bsd::accept(listen_fd, nullptr, nullptr);
                auto connection = conn[nfd] = std::make_shared<HTTPConnection>();
                connection->fd = nfd;

                evt.data.fd = nfd;
                socket::bsd::epoll_ctl(epfd, EPOLL_CTL_ADD, nfd, &evt);
                OMNI_LOG(common::kInfo) << "New connection" << std::endl;
            } else {
                if (e->events & EPOLLIN) {
                    auto connection = conn[e->data.fd];
                    auto ret = socket::bsd::read(connection->fd, 
                        (unsigned char*)connection->request + connection->request_recv,
                        HTTP_HEADER_LEN - connection->request_recv);
                    
                    if (ret == 0) {
                        socket::bsd::close(connection->fd);
                        conn.erase(e->data.fd);
                        continue;
                    }

                    connection->request_recv += ret;
                    connection->request[connection->request_recv] = '\0';
                    connection->request_hdr_len = find_http_header(connection->request, connection->request_recv);

                    if (connection->request_hdr_len > 0) {
                        // Decode and start transmitting
                        connection->request[connection->request_hdr_len-1] = '\0';
                        OMNI_LOG(common::kInfo) << "New http request received" << std::endl;

                        char url[MAX_URL_LEN];
                        http_get_url(connection->request, connection->request_hdr_len-1, url, MAX_URL_LEN);
                        connection->file_name = args::dir + std::string(url);
                        OMNI_LOG(common::kInfo) << "File name: " << connection->file_name << std::endl;

                        char keepalive_str[128];
                        if (http_header_str_val(connection->request, "Connection: ", strlen("Connection: "), keepalive_str, 128)) {	
                            if (strstr(keepalive_str, HTTP_KEEP_ALIVE)) {
                                connection->keep_alive = true;
                            }
                        }

                        int status_code = 404;
                        connection->file = cacheFile(connection->file_name);
                        if (connection->file != nullptr) {
                            status_code = 200;
                            connection->fsize = connection->file->file_size;
                            connection->sent = 0;
                        }

                        time_t t_now;
                        time(&t_now);
                        char t_str[128];
                        strftime(t_str, 128, "%a, %d %b %Y %X GMT", gmtime(&t_now));
                        sprintf(connection->response, "HTTP/1.1 %d %s\r\n"
                                "Date: %s\r\n"
                                "Server: Webserver on Middlebox TCP (Ubuntu)\r\n"
                                "Content-Length: %ld\r\n"
                                "Connection: %s\r\n\r\n", 
                                status_code, StatusCodeToString(status_code), t_str, connection->fsize, keepalive_str);
                        connection->response_hdr_len = strlen(connection->response);

                        SendResponse(connection);

                        if (!connection->keep_alive) {
                            socket::bsd::close(connection->fd);
                            conn.erase(e->data.fd);
                        } else {
                            auto rest_len = connection->request_recv - connection->request_hdr_len;
                            for (int i = 0; i < rest_len; i ++) {
                                connection->request[i] = connection->request[i + connection->request_hdr_len];
                            }
                            connection->request_recv -= rest_len;
                        }
                    }
                }
            }
        }
    }

    return 0;
}