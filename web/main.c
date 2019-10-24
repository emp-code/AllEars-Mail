#include <arpa/inet.h>
#include <dirent.h>
#include <fcntl.h>
#include <pwd.h>
#include <grp.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <locale.h> // for setlocale

#include <sodium.h>
#include <mbedtls/ssl.h>

#include "Include/Brotli.h"
#include "aem_file.h"
#include "https.h"

#define AEM_PORT_HTTPS 443
#define AEM_PATH_TLSKEY "/etc/allears/TLS.key"
#define AEM_PATH_TLSCRT "/etc/allears/TLS.crt"

static bool terminate = false;

static void sigTerm() {
	puts("Terminating after handling next connection");
	terminate = true;
}

__attribute__((warn_unused_result))
static int dropRoot(void) {
	const struct passwd * const p = getpwnam("nobody");
	if (p == NULL) return -1;

	if (setgid(p->pw_gid) != 0) return -1;
	if (setuid(p->pw_uid) != 0) return -1;

	if (getgid() != p->pw_gid || getuid() != p->pw_uid) return -1;

	return 0;
}

__attribute__((warn_unused_result))
static int initSocket(const int * const sock, const int port) {
	struct sockaddr_in servAddr;
	bzero((char*)&servAddr, sizeof(servAddr));
	servAddr.sin_family = AF_INET;
	servAddr.sin_addr.s_addr = htonl(INADDR_ANY);
	servAddr.sin_port = htons(port);

	const int optval = 1;
	setsockopt(*sock, SOL_SOCKET, SO_REUSEPORT, (const void*)&optval, sizeof(int));

	const int ret = bind(*sock, (struct sockaddr*)&servAddr, sizeof(servAddr));
	if (ret < 0) return ret;

	listen(*sock, 10); // socket, backlog (# of connections to keep in queue)
	return 0;
}

__attribute__((warn_unused_result))
static int aem_countFiles(const char * const path, const char * const ext, const size_t extLen) {
	DIR * const dir = opendir(path);
	if (dir == NULL) return 0;

	int counter = 0;

	while(1) {
		const struct dirent * const de = readdir(dir);
		if (de == NULL) break;
		if (memcmp(de->d_name + strlen(de->d_name) - extLen, ext, extLen) == 0) counter++;
	}

	closedir(dir);
	return counter;
}

__attribute__((warn_unused_result))
static struct aem_file *aem_loadFiles(const char * const path, const char * const ext, const size_t extLen, const int fileCount) {
	if (path == NULL || ext == NULL || extLen < 1 || fileCount < 1) return NULL;

	DIR * const dir = opendir(path);
	if (dir == NULL) return NULL;

	struct aem_file * const f = sodium_allocarray(fileCount, sizeof(struct aem_file));
	if (f == NULL) return NULL;

	for (int counter = 0; counter < fileCount;) {
		const struct dirent * const de = readdir(dir);
		if (de == NULL) {f[counter].lenData = 0; break;}

		if (memcmp(de->d_name + strlen(de->d_name) - extLen, ext, extLen) == 0) {
			char filePath[strlen(path) + strlen(de->d_name) + 1];
			sprintf(filePath, "%s/%s", path, de->d_name);

			const int fd = open(filePath, O_RDONLY);
			if (fd < 0) {f[counter].lenData = 0; continue;}
			const off_t bytes = lseek(fd, 0, SEEK_END);

			if (strcmp(ext, ".css") == 0 || strcmp(ext, ".html") == 0 || strcmp(ext, ".js") == 0) {
				// Files to be compressed
				char *tempData = malloc(bytes);
				if (tempData == NULL) {printf("Terminating: Failed to allocate memory for loading %s\n", de->d_name); break;}

				const ssize_t readBytes = pread(fd, tempData, bytes, 0);
				close(fd);

				if (readBytes == bytes) {
					brotliCompress(&tempData, (size_t*)&bytes);

					f[counter].filename = strdup(de->d_name);
					f[counter].lenData = bytes;

					f[counter].data = sodium_malloc(bytes);
					if (f[counter].data == NULL) {printf("Terminating: Failed to allocate memory (Sodium) for loading %s\n", de->d_name); break;}
					memcpy(f[counter].data, tempData, bytes);
					sodium_mprotect_readonly(f[counter].data);
					free(tempData);

					printf("Loaded %s (%zd bytes compressed)\n", f[counter].filename, f[counter].lenData);
				} else {
					printf("Failed to load %s\n", de->d_name);
					free(tempData);
				}
			} else {
				// Files not to be compressed
				f[counter].data = sodium_malloc(bytes);
				if (f[counter].data == NULL) {printf("Terminating: Failed to allocate memory (Sodium) for loading %s\n", de->d_name); break;}

				const ssize_t readBytes = pread(fd, f[counter].data, bytes, 0);
				close(fd);

				if (readBytes == bytes) {
					sodium_mprotect_readonly(f[counter].data);

					f[counter].lenData = bytes;
					f[counter].filename = strdup(de->d_name);

					printf("Loaded %s (%zd bytes)\n", f[counter].filename, f[counter].lenData);
				} else {
					printf("Failed to load %s\n", de->d_name);
					sodium_free(f[counter].data);
				}
			}

			counter++;
		}
	}

	sodium_mprotect_readonly(f);
	closedir(dir);
	return f;
}

__attribute__((warn_unused_result))
static int loadTlsKey(mbedtls_pk_context * const key) {
	mbedtls_pk_init(key);
	const int ret = mbedtls_pk_parse_keyfile(key, AEM_PATH_TLSKEY, NULL);
	if (ret == 0) return 0;

	printf("mbedtls_pk_parse_key returned %d\n", ret);
	return 1;
}

static int receiveConnections(const char * const domain, const size_t lenDomain, mbedtls_x509_crt * const tlsCert) {
	if (access("html/index.html", R_OK) == -1 ) {
		puts("Terminating: missing html/index.html");
		return 1;
	}

	mbedtls_pk_context tlsKey;
	if (loadTlsKey(&tlsKey) < 0) return 1;

	const int numCss  = aem_countFiles("css",  ".css",  4);
	const int numHtml = aem_countFiles("html", ".html", 5);
	const int numImg  = aem_countFiles("img",  ".png",  4);
	const int numJs   = aem_countFiles("js",   ".js",   3);

	printf("Loading files: %d CSS, %d HTML, %d image, %d Javascript\n", numCss, numHtml, numImg, numJs);

	struct aem_file * const fileCss  = aem_loadFiles("css",  ".css",  4, numCss);
	struct aem_file * const fileHtml = aem_loadFiles("html", ".html", 5, numHtml);
	struct aem_file * const fileImg  = aem_loadFiles("img",  ".png",  4, numImg);
	struct aem_file * const fileJs   = aem_loadFiles("js",   ".js",   3, numJs);

	struct aem_fileSet * const fileSet = sodium_malloc(sizeof(struct aem_fileSet));
	if (fileSet == NULL) {puts("Failed to allocate memory for fileSet"); return 1;}
	fileSet->cssFiles  = fileCss;
	fileSet->htmlFiles = fileHtml;
	fileSet->imgFiles  = fileImg;
	fileSet->jsFiles   = fileJs;
	fileSet->cssCount  = numCss;
	fileSet->htmlCount = numHtml;
	fileSet->imgCount  = numImg;
	fileSet->jsCount   = numJs;
	sodium_mprotect_readonly(fileSet);

	const int sock = socket(AF_INET, SOCK_STREAM, 0);

	int ret = 0;
	if (sock < 0) {ret = -2;}
	if (ret == 0) {if (initSocket(&sock, AEM_PORT_HTTPS) != 0) ret = -3;}
	if (ret == 0) {if (dropRoot() != 0) ret = -4;}

	if (ret == 0) {
		puts("Ready");

		while(!terminate) {
			const int newSock = accept(sock, NULL, NULL);
			if (newSock < 0) {puts("Failed to create socket for accepting connection"); break;}
			respond_https(newSock, tlsCert, &tlsKey, domain, lenDomain, fileSet);
			close(newSock);
		}
	}

	for (int i = 0; i < numCss;  i++) {free(fileCss[i].filename);  sodium_free(fileCss[i].data);}
	for (int i = 0; i < numHtml; i++) {free(fileHtml[i].filename); sodium_free(fileHtml[i].data);}
	for (int i = 0; i < numImg;  i++) {free(fileImg[i].filename);  sodium_free(fileImg[i].data);}
	for (int i = 0; i < numJs;   i++) {free(fileJs[i].filename);   sodium_free(fileJs[i].data);}

	sodium_free(fileCss);
	sodium_free(fileHtml);
	sodium_free(fileImg);
	sodium_free(fileJs);
	sodium_free(fileSet);

	mbedtls_x509_crt_free(tlsCert);
	mbedtls_pk_free(&tlsKey);
	close(sock);
	return 0;
}

__attribute__((warn_unused_result))
char *getDomainInfo(mbedtls_x509_crt * const cert) {
	char certInfo[1000];
	mbedtls_x509_crt_info(certInfo, 1000, "AEM_", cert);

	char *c = strstr(certInfo, "\nAEM_subject name");
	if (c == NULL) return NULL;
	c += 17;

	char * const end = strchr(c, '\n');
	*end = '\0';

	c = strstr(c, ": CN=");
	if (c == NULL) return NULL;
	return strdup(c + 5);
}

__attribute__((warn_unused_result))
size_t getDomainLenFromCert(mbedtls_x509_crt * const cert) {
	char * const c = getDomainInfo(cert);
	if (c == NULL) return 0;
	const size_t s = strlen(c);
	free(c);
	return s;
}

__attribute__((warn_unused_result))
int getDomainFromCert(char * const dom, const size_t len, mbedtls_x509_crt * const cert) {
	char * const c = getDomainInfo(cert);
	if (c == NULL) return -1;
	memcpy(dom, c, len);
	free(c);
	return 0;
}

int main(void) {
	if (getuid() != 0) {
		puts("Terminating: Must be started as root");
		return EXIT_FAILURE;
	}

	if (signal(SIGPIPE, SIG_IGN) == SIG_ERR) { // Prevent writing to closed/invalid sockets from ending the process
		puts("Terminating: signal failed");
		return EXIT_FAILURE;
	}

	signal(SIGINT, sigTerm);
	signal(SIGQUIT, sigTerm);
	signal(SIGTERM, sigTerm);

	if (sodium_init() < 0) {
		puts("Terminating: Failed to initialize libsodium");
		return EXIT_FAILURE;
	}

	setlocale(LC_ALL, "C");

	// Get domain from TLS certificate
	mbedtls_x509_crt tlsCert;
	mbedtls_x509_crt_init(&tlsCert);
	int ret = mbedtls_x509_crt_parse_file(&tlsCert, AEM_PATH_TLSCRT);
	if (ret != 0) {
		printf("Terminating: mbedtls_x509_crt_parse returned %d\n", ret);
		return EXIT_FAILURE;
	}

	const size_t lenDomain = getDomainLenFromCert(&tlsCert);
	char domain[lenDomain];
	ret = getDomainFromCert(domain, lenDomain, &tlsCert);
	if (ret != 0) {puts("Terminating: Failed to get domain from certificate"); return EXIT_FAILURE;}

	printf("Domain detected as '%.*s'\n", (int)lenDomain, domain);

	return receiveConnections(domain, lenDomain, &tlsCert);
}