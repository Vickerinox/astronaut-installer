// SPDX-License-Identifier: Apache-2.0 OR GPL-2.0-or-later
//
// Copyright The Mbed TLS Contributors
// Copyright 2025 Antonio Niño Díaz

// SSL client demonstration program. This is a modified version of the following
// example provided by Mbed TLS:
//
// https://github.com/Mbed-TLS/mbedtls/blob/v3.6.4/programs/ssl/ssl_client1.c
//
// Internally mbedtls_net_connect() connects to either IPv4 or IPv6 addresses
// depending on what it can resolve.

#include "update.h"
#include <cstddef>
#include <cstdio>
#include <stdio.h>
#include <unistd.h>
#include "main.h"
#include "message.h"
#include "sha1digest.h"
#include "storage.h"
#include <mbedtls/build_info.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/debug.h>
#include <mbedtls/entropy.h>
#include <mbedtls/error.h>
#include <mbedtls/net_sockets.h>
#include <mbedtls/platform.h>
#include <mbedtls/ssl.h>

#include <dswifi9.h>
#include <filesystem.h>
#include <nds.h>
#include <cstring>

// Default port for SSL
#define SERVER_PORT "443"

// Change this to get debug messages from Mbed TLS in the console
#define DEBUG_LEVEL 0






// This function will be called when Mbed TLS prints debug messages
static void my_debug(void *ctx, int level, const char *file, int line,
                     const char *str)
{
    (void)ctx;
    (void)level;

    printf("%s:%04d: %s", file, line, str);
}

void HttpsParser::parse_line() {
    if(!in_header) {
        printf("ERROR IN PARSER\n");
        return;
    }
    if(line == "") {
        in_header = false;
    } else if (line.starts_with("HTTP/1.1 ")) {
        response = stoi(line.substr(9, 3));
    } else if (line.starts_with("Location: https://")) {
        if(response == 302) {
            redirect = line.substr(18);
        }
    } else if (line.starts_with("Content-Length: ")) {
        content_len = stoi(line.substr(16));
    }
    line = "";
}
void HttpsParser::reset() {
    line = "";
    in_header = true;
    response = 0;
    redirect = "";
    content_len = -1;
    show_progress = true;
}

int HttpsParser::getWebsiteSSL(const char *certs, const char *host, const char *path, FILE* response_save)
{
    reset();

    int error = UPDATE_UNKNOWN_ERROR;
    int downloaded = 0;
    int ret = 1, len;
    int exit_code = MBEDTLS_EXIT_FAILURE;
    mbedtls_net_context server_fd;
    uint32_t flags;
    unsigned char buf[2048];
    const char *pers = "ssl_client1";


    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context ctr_drbg;
    mbedtls_ssl_context ssl;
    mbedtls_ssl_config conf;
    mbedtls_x509_crt cacert;

    // 0. Initialize the RNG and the session data

    mbedtls_net_init(&server_fd);
    mbedtls_ssl_init(&ssl);
    mbedtls_ssl_config_init(&conf);
    mbedtls_x509_crt_init(&cacert);
    mbedtls_ctr_drbg_init(&ctr_drbg);
    mbedtls_entropy_init(&entropy);

    ret = mbedtls_ctr_drbg_seed(&ctr_drbg, mbedtls_entropy_func, &entropy,
                                (const unsigned char *)pers, strlen(pers));
    if (ret != 0)
    {
        error = UPDATE_BAD_TLS_SEED;
        goto exit;
    }

    // 1. Initialize certificates
    // In Linux, for example, they are stored in /etc/ssl/certs/
    
    ret = mbedtls_x509_crt_parse_file(&cacert, certs);
    if (ret < 0)
    {
        error = UPDATE_BAD_TLS_CERTS;
        goto exit;
    }

    ret = mbedtls_net_connect(&server_fd, host, SERVER_PORT,
                              MBEDTLS_NET_PROTO_TCP); // TCP
    if (ret != 0)
    {
        error = UPDATE_BAD_TLS_CONNECT;
        goto exit;
    }

    // 3. Setup stuff

    ret = mbedtls_ssl_config_defaults(&conf,
                                      MBEDTLS_SSL_IS_CLIENT,
                                      MBEDTLS_SSL_TRANSPORT_STREAM, // TCP
                                      MBEDTLS_SSL_PRESET_DEFAULT);
    if (ret != 0)
    {
        error = UPDATE_BAD_TLS_CONFIG;
        goto exit;
    }

    // MBEDTLS_SSL_VERIFY_OPTIONAL means that the certificates are checked but
    // any failure is ignored, which is useful for debugging. You need to set
    // DEBUG_LEVEL to a non-zero value if you want to see the error messages.
    //
    // If you use MBEDTLS_SSL_VERIFY_REQUIRED Mbed TLS will refuse to connect if
    // the certificates aren't valid, which is the right setting for a finished
    // program.
    mbedtls_ssl_conf_authmode(&conf, MBEDTLS_SSL_VERIFY_REQUIRED);
    mbedtls_ssl_conf_ca_chain(&conf, &cacert, NULL);
    mbedtls_ssl_conf_rng(&conf, mbedtls_ctr_drbg_random, &ctr_drbg);
    mbedtls_ssl_conf_dbg(&conf, my_debug, stdout);

    ret = mbedtls_ssl_setup(&ssl, &conf);
    if (ret != 0)
    {
        error = UPDATE_BAD_TLS_SETUP;
        goto exit;
    }

    ret = mbedtls_ssl_set_hostname(&ssl, host);
    if (ret != 0)
    {
        error = UPDATE_BAD_TLS_HOSTNAME;
        goto exit;
    }

    mbedtls_ssl_set_bio(&ssl, &server_fd, mbedtls_net_send, mbedtls_net_recv, NULL);

    // 4. Handshake

    while (1)
    {
        ret = mbedtls_ssl_handshake(&ssl);
        if (ret == 0)
            break;

        if (ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE)
        {
            error = UPDATE_BAD_TLS_HANDSHAKE;
            goto exit;
        }
    }

    // 5. Verify the server certificate


    // In real life, we probably want to exit when ret != 0
    flags = mbedtls_ssl_get_verify_result(&ssl);
    if (flags != 0)
    {
        error = UPDATE_BAD_TLS_VERIFY;
    }

    // 6. Send the GET request


    len = snprintf((char *)buf, sizeof(buf),
                   "GET %s HTTP/1.1\r\n"
                   "Host: %s\r\n"
                   "User-Agent: Nintendo DS\r\n"
                   "Connection: close\r\n"
                   "\r\n", path, host);

    while (1)
    {
        ret = mbedtls_ssl_write(&ssl, buf, len);
        if (ret > 0)
            break; 

        if (ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE)
        {
            goto exit;
        }
    }

    len = ret;

    while (1)
    {
        len = sizeof(buf) - 1;
        memset(buf, 0, sizeof(buf));
        ret = mbedtls_ssl_read(&ssl, buf, len);

        if (ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE)
        {   
            continue;
        }

        if (ret == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY)
        {
            break;
        }

        if (ret < 0)
        {
            error = UPDATE_BAD_DOWNLOAD_COMM;
            break;
        }

        if (ret == 0)
        {
            // EOF
            break;
        }

        len = ret;

        {
            
            int i = 0;
            while(in_header && i < len) {
                char c = buf[i++];
                if (c == '\r')
                    continue;
                else if (c == '\n')
                    parse_line();
                else
                    line += c;
            }
            if (!in_header && content_len > downloaded) {
                int available_data = len-i;
                int av_len = available_data;
                int written = fwrite(&buf[i], sizeof(char), av_len, response_save);
                downloaded += written;
                if(written != av_len) {
                    error = UPDATE_BAD_DOWNLOAD_WRITE;
                }
            }
            if(show_progress) {
                if(response == 200) {
                    cothread_yield_irq(IRQ_VBLANK);
                    clearScreen(&bottomScreen);
                    printProgessBar("Downloading", downloaded, content_len);
                }  
            } 
        }
        cothread_yield();
    }
    mbedtls_ssl_close_notify(&ssl);

    if (ret == 0 || ret == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY)
    {
        exit_code = MBEDTLS_EXIT_SUCCESS;
        error = UPDATE_SUCCESS;
    }

exit:

    if (exit_code != MBEDTLS_EXIT_SUCCESS)
    {
        if(!error)
            error = UPDATE_BAD_TLS_FINISH;
    }
    mbedtls_net_free(&server_fd);
    mbedtls_x509_crt_free(&cacert);
    mbedtls_ssl_free(&ssl);
    mbedtls_ssl_config_free(&conf);
    mbedtls_ctr_drbg_free(&ctr_drbg);
    mbedtls_entropy_free(&entropy);
    return error;
}

HttpsParser::HttpsParser() {
    reset();
}
int HttpsParser::download_file_from_github(const char* url, const char* store_to) {
    int redirects = 5;
    int ret = 0;
    
    while(redirects) {
        FILE* log = fopen(store_to, "wb+");
        if(!log) {
            redirects = 0;
            ret = UPDATE_BAD_FILE_OPEN_TEMP;
            break;
        }
        if(redirect != "") {
            int spot = redirect.find("/");
            if(spot > 0 && response == 302){
                char const* current_host = strdup(redirect.substr(0, spot).c_str());
                char const* current_line = strdup(redirect.substr(spot).c_str());
                char const* banan = NULL;
                if(strcmp(current_host, "github.com") == 0) {
                    banan = "/github-com-chain.pem";
                } else {
                    banan = "/github-io-chain.pem";
                }
                if(int error = getWebsiteSSL(banan,current_host, current_line, log)) {
                    ret = error;
                    redirects = 0;
                } else {
                    if(response == 200) {
                        redirects = 0;
                    } else if (response == 302) {
                        redirects--;
                    } else {
                        ret = UPDATE_BAD_HTTP_CODE;
                        redirects = 0;
                    }
                }
            } else {
                ret = UPDATE_BAD_REDIRECT;
                redirects = 0;
            }
        } else { 
            if(int error = getWebsiteSSL("/github-com-chain.pem","github.com",url, log)) {
                ret = error;
                redirects = 0;
            } else {
                redirects--;
            }
        }
        fclose(log);
    }
    return ret;
}
int check_version(FILE* fil, int& expected_len, Sha1Digest& expected_sha1,std::string& version) {
    char buffer[256] = {0};
    int len = fread(buffer, sizeof(char), 256, fil);
    if(len > 0) {
        char* ver = strtok(buffer, "\r\n");
        if(ver != NULL) {
            version = std::string(ver);
            char* verstring = versionString();
            char* param = strtok(NULL, "\r\n");
            while(param != NULL) {
                if(strncmp(param, "Length ", 7) == 0) {
                    expected_len = atoi(param+7);
                } else if(strncmp(param, "Sha1 ", 5) == 0) {
                    expected_sha1 = Sha1Digest{param+5};
                }
                param = strtok(NULL, "\r\n");
            }
            if(strcmp(ver, verstring) == 0) {
                return UPDATE_VERSION_ALREADY_LATEST;
            } else {
                return UPDATE_SUCCESS;
            }
        } else {
            return UPDATE_VERSION_CHECK_FAILED;
        }
    } 
    return UPDATE_VERSION_CHECK_FAILED;
}
bool initWifi = true;
int lookForUpdates(const std::string& install_location)
{

    
    // create install locations
    std::string temp_install_location = install_location + ".temp";
    std::string meta_install_location = install_location + ".meta";

    #define META_URL_PATH "/Vickerinox/astronaut-installer/releases/latest/download/astronaut-installer-meta.txt"
    #define TEMP_INSTALLER_URL_PATH "/Vickerinox/astronaut-installer/releases/latest/download/astronaut-installer.dsi"

    // look for file conflicts
    if(fileExists(meta_install_location.c_str()) || fileExists(temp_install_location.c_str())) {
        if(!choiceBox("Temporary install files already\nexist on this sd card.\n\nTry to update anyway?")) {
            return UPDATE_CANCELLED;
        }
    }

    clearScreen(&bottomScreen);
    printf("Connecting to Wifi...");
    // Initialize WiFi
    if(initWifi) {
        if(Wifi_InitDefault(INIT_ONLY | WIFI_ATTEMPT_DSI_MODE))
            initWifi = false;
        else 
            return UPDATE_BAD_WIFI_INIT;
    }
    int num_wfc_caps = Wifi_GetData(WIFIGETDATA_NUMWFCAPS, 0, NULL);
    if (num_wfc_caps <= 0)
        return UPDATE_NO_APS;
    
    cothread_yield_irq(IRQ_VBLANK);
    Wifi_EnableWifi();
    cothread_yield_irq(IRQ_VBLANK);
    Wifi_AutoConnect();
    cothread_yield_irq(IRQ_VBLANK);

    int timer = 0;
    int retries = 0;
    int ret = 0;
    while (1)
    {      
        
        int status = Wifi_AssocStatus();
        char const* msg = NULL;
        timer++;
        switch(status) {
            case ASSOCSTATUS_DISCONNECTED:
            msg = "Disconnected";
            break;
            case ASSOCSTATUS_SEARCHING:
            msg = "Searching for access point";
            break;
            case ASSOCSTATUS_AUTHENTICATING:
            msg = "Authenticating";
            break;
            case ASSOCSTATUS_ASSOCIATING:
            msg = "Connecting";
            break;
            case ASSOCSTATUS_ACQUIRINGDHCP:
            msg = "Gathering IP address";
            break;
            case ASSOCSTATUS_ASSOCIATED:
            msg = "Connected";
            break;
            case ASSOCSTATUS_CANNOTCONNECT:
            msg = "Couldn't connect";
            break;
        }
        if (status == ASSOCSTATUS_ASSOCIATED)
        {
            break;
        }
        
        if(timer > 1024 || status == ASSOCSTATUS_CANNOTCONNECT || status == ASSOCSTATUS_DISCONNECTED) {
            Wifi_DisconnectAP();
            Wifi_DisableWifi();
            Wifi_EnableWifi();
            Wifi_AutoConnect();
            retries++;
            timer = 0;
        }
        if(retries > 5) {
            ret = UPDATE_NO_INTERNET;
            break;
        }
        char const* throbber = NULL;
        switch((timer>>2) & 3) {
            case 0:
            throbber = "-";
            break;
            case 1:
            throbber = "\\";
            break;
            case 2:
            throbber = "|";
            break;
            case 3:
            throbber = "/";
            break;
        }
        cothread_yield_irq(IRQ_VBLANK);
        clearScreen(&bottomScreen);
        if (retries) 
            printf("%s \x1b[0;28H%s\n(retry %d)", msg, throbber, retries);
        else 
            printf("%s \x1b[0;28H%s\n", msg, throbber);
    }
    if(ret) {
        goto exit_no_files;
    }
    // Find out what the latest version is
    {
        clearScreen(&bottomScreen);
        HttpsParser parser = HttpsParser();
        parser.disableProgressBar();
        clearScreen(&bottomScreen);
        printf("Searching for latest version...");
        ret = parser.download_file_from_github(META_URL_PATH, meta_install_location.c_str());
        
    }

    
    if(ret) {
        goto exit_no_files;
    }
    {
        Sha1Digest expected_sha1;
        std::string version;
        int expected_len = 0;
        // Decode the new version data
        FILE* shid = fopen(meta_install_location.c_str(), "rb");
        if (!shid) {
            ret = UPDATE_BAD_FILE_OPEN_META;
            goto exit;
        }    
        
        if(int error = check_version(shid, expected_len, expected_sha1, version)) {
            if(error != UPDATE_VERSION_ALREADY_LATEST) {
                ret = error;
                goto exit;
            }
            if(!choiceBox("You're already up to date,\ndo you want to update anyway?")) {
                ret = UPDATE_CANCELLED;
                goto exit;
            }
        } 
        if(expected_len <= 0 || expected_sha1 == Sha1Digest{}) {
            ret = UPDATE_VERSION_INVALID;
            goto exit;
        }
        
        // Ask if we want to download the new version
        {
            char buffer[256] = {0};
            snprintf(buffer, sizeof(buffer), "Update available!\n\nVersion: %s\n\nupdate now?", version.c_str());

            if (!choiceBox(buffer)) {
                ret = UPDATE_CANCELLED;
            }
        }
        if (ret) {
            goto exit;
        }

        // Download the new version
        {
            HttpsParser parser = HttpsParser();
            clearScreen(&bottomScreen);
            printf("Connecting to github...");
            ret = parser.download_file_from_github(TEMP_INSTALLER_URL_PATH, temp_install_location.c_str());
        }
        if (ret) {
            goto exit;
        }

        // Verify the new version
        if(FILE* shid2 = fopen(temp_install_location.c_str(), "rb")){
            Sha1Digest actual_digest;
            if(!calculateFileSha1ShowProgress(shid2, &actual_digest, expected_len)) {
                ret = UPDATE_BAD_VERIFY;
            }
            long len = ftell(shid2);
            fclose(shid2);
            if(actual_digest != expected_sha1) {
                ret = UPDATE_BAD_SHA1;
            }
            if(len != expected_len) {
                ret = UPDATE_BAD_LENGTH;
            }
        } else {
            ret = UPDATE_BAD_FILE_OPEN_INSTALL;
        }
        
        // Cleanup
        exit:
        fclose(shid);
    }
    exit_no_files:

    if(ret == UPDATE_SUCCESS) {
        if(fileExists(install_location.c_str())) {
            if(remove(install_location.c_str())) {
                ret = UPDATE_BAD_CLEANUP_DELORIGINAL;
            } else if(rename(temp_install_location.c_str(), install_location.c_str())) {
                ret = UPDATE_BAD_CLEANUP_RENAME;
            }
        }
    } else if(fileExists(temp_install_location.c_str())) {
        if(remove(temp_install_location.c_str())) {
            ret = UPDATE_BAD_CLEANUP_DELTEMP;
        }
    }
    
    if(fileExists(meta_install_location.c_str())) {
        if(remove(meta_install_location.c_str())) {
            ret = UPDATE_BAD_CLEANUP_DELMETA;
        }
    }
    
    if (Wifi_DisconnectAP() != 0)
        ret = UPDATE_BAD_WIFI_UNINIT;
    Wifi_DisableWifi();
    return ret;
}