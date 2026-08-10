#ifndef UPDATE_H
#define UPDATE_H

#include <string>


#define UPDATE_SUCCESS 0

#define UPDATE_UNKNOWN_ERROR -1
#define UPDATE_CANCELLED -2
#define UPDATE_VERSION_ALREADY_LATEST -3
#define UPDATE_VERSION_INVALID -4
#define UPDATE_VERSION_CHECK_FAILED -5
#define UPDATE_BAD_WIFI_INIT -6
#define UPDATE_BAD_WIFI_UNINIT -7
#define UPDATE_BAD_TLS_SEED -8
#define UPDATE_BAD_TLS_CERTS -9
#define UPDATE_BAD_TLS_CONNECT -10
#define UPDATE_BAD_TLS_CONFIG -11
#define UPDATE_BAD_TLS_SETUP -12
#define UPDATE_BAD_TLS_HOSTNAME -13
#define UPDATE_BAD_TLS_HANDSHAKE -14
#define UPDATE_BAD_TLS_VERIFY -15
#define UPDATE_BAD_DOWNLOAD_COMM -16
#define UPDATE_BAD_DOWNLOAD_WRITE -17
#define UPDATE_BAD_TLS_FINISH -18
#define UPDATE_BAD_HTTP_CODE -19
#define UPDATE_BAD_REDIRECT -20
#define UPDATE_BAD_LENGTH -21
#define UPDATE_BAD_SHA1 -22
#define UPDATE_BAD_VERIFY -23
#define UPDATE_BAD_FILE_OPEN_META -24
#define UPDATE_BAD_FILE_OPEN_TEMP -25
#define UPDATE_BAD_FILE_OPEN_INSTALL -26

#define UPDATE_NO_INTERNET -27

#define UPDATE_BAD_CLEANUP_DELORIGINAL 1
#define UPDATE_BAD_CLEANUP_DELTEMP 2
#define UPDATE_BAD_CLEANUP_DELMETA 3
#define UPDATE_BAD_CLEANUP_RENAME 4


class HttpsParser {
    
    std::string line;
    std::string redirect;
    int content_len;
    int response;
    bool in_header;
    bool show_progress;
    char const* show_message;
    
    int getWebsiteSSL(const char *certs, const char *host, const char *path, FILE* response_save);
    void parse_line();
    void reset();

    public:
    HttpsParser();
    int download_file_from_github(const char* url, const char* store_to);
    void disableProgressBar() {
        show_progress = false;
    }
};  

int lookForUpdates(std::string const& install_location);


#endif