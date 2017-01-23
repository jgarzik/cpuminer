#ifndef __UTIL_H__
#define __UTIL_H__

#include <semaphore.h>

#if defined(unix) || defined(__APPLE__)
	#include <errno.h>
	#include <sys/socket.h>
	#include <netinet/in.h>
	#include <arpa/inet.h>

	#define SOCKETTYPE long
	#define SOCKETFAIL(a) ((a) < 0)
	#define INVSOCK -1
	#define INVINETADDR -1
	#define CLOSESOCKET close
	#define INET_PTON inet_pton

	#define SOCKERRMSG strerror(errno)
	static inline bool sock_blocks(void)
	{
		return (errno == EAGAIN || errno == EWOULDBLOCK);
	}
	static inline bool sock_timeout(void)
	{
		return (errno == ETIMEDOUT);
	}
	static inline bool interrupted(void)
	{
		return (errno == EINTR);
	}
#elif defined WIN32
	#include <winsock2.h>
	#include <windows.h>
	#include <ws2tcpip.h>

	#define SOCKETTYPE SOCKET
	#define SOCKETFAIL(a) ((int)(a) == SOCKET_ERROR)
	#define INVSOCK INVALID_SOCKET
	#define INVINETADDR INADDR_NONE
	#define CLOSESOCKET closesocket

	int Inet_Pton(int af, const char *src, void *dst);
	#define INET_PTON Inet_Pton

	extern char *WSAErrorMsg(void);
	#define SOCKERRMSG WSAErrorMsg()

	/* Check for windows variants of the errors as well as when ming
	 * decides to wrap the error into the errno equivalent. */
	static inline bool sock_blocks(void)
	{
		return (WSAGetLastError() == WSAEWOULDBLOCK || errno == EAGAIN);
	}
	static inline bool sock_timeout(void)
	{
		return (WSAGetLastError() == WSAETIMEDOUT || errno == ETIMEDOUT);
	}
	static inline bool interrupted(void)
	{
		return (WSAGetLastError() == WSAEINTR || errno == EINTR);
	}
	#ifndef SHUT_RDWR
	#define SHUT_RDWR SD_BOTH
	#endif

	#ifndef in_addr_t
	#define in_addr_t uint32_t
	#endif
#endif

#define JSON_LOADS(str, err_ptr) json_loads((str), 0, (err_ptr))

#ifdef HAVE_LIBCURL
#include <curl/curl.h>
typedef curl_proxytype proxytypes_t;
#else
typedef int proxytypes_t;
#endif /* HAVE_LIBCURL */

/* cgminer locks, a write biased variant of rwlocks */
struct cglock {
	pthread_mutex_t mutex;
	pthread_rwlock_t rwlock;
};

typedef struct cglock cglock_t;

/* cgminer specific unnamed semaphore implementations to cope with osx not
 * implementing them. */
#ifdef __APPLE__
struct cgsem {
	int pipefd[2];
};

typedef struct cgsem cgsem_t;
#else
typedef sem_t cgsem_t;
#endif
#ifdef WIN32
typedef LARGE_INTEGER cgtimer_t;
#else
typedef struct timespec cgtimer_t;
#endif

extern int no_yield(void);
extern int (*selective_yield)(void);
void *_cgmalloc(size_t size, const char *file, const char *func, const int line);
void *_cgcalloc(const size_t memb, size_t size, const char *file, const char *func, const int line);
void *_cgrealloc(void *ptr, size_t size, const char *file, const char *func, const int line);
#define cgmalloc(_size) _cgmalloc(_size, __FILE__, __func__, __LINE__)
#define cgcalloc(_memb, _size) _cgcalloc(_memb, _size, __FILE__, __func__, __LINE__)
#define cgrealloc(_ptr, _size) _cgrealloc(_ptr, _size, __FILE__, __func__, __LINE__)
struct thr_info;
struct pool;
enum dev_reason;
struct cgpu_info;
void b58tobin(unsigned char *b58bin, const char *b58);
void address_to_pubkeyhash(unsigned char *pkh, const char *addr);
int ser_number(unsigned char *s, int32_t val);
unsigned char *ser_string(char *s, int *slen);
int thr_info_create(struct thr_info *thr, pthread_attr_t *attr, void *(*start) (void *), void *arg);
void thr_info_cancel(struct thr_info *thr);
void cgtime(struct timeval *tv);
void subtime(struct timeval *a, struct timeval *b);
void addtime(struct timeval *a, struct timeval *b);
bool time_more(struct timeval *a, struct timeval *b);
bool time_less(struct timeval *a, struct timeval *b);
void copy_time(struct timeval *dest, const struct timeval *src);
void timespec_to_val(struct timeval *val, const struct timespec *spec);
void timeval_to_spec(struct timespec *spec, const struct timeval *val);
void us_to_timeval(struct timeval *val, int64_t us);
void us_to_timespec(struct timespec *spec, int64_t us);
void ms_to_timespec(struct timespec *spec, int64_t ms);
void timeraddspec(struct timespec *a, const struct timespec *b);
char *Strcasestr(char *haystack, const char *needle);
char *Strsep(char **stringp, const char *delim);
void cgsleep_ms(int ms);
void cgsleep_us(int64_t us);
void cgtimer_time(cgtimer_t *ts_start);
#define cgsleep_prepare_r(ts_start) cgtimer_time(ts_start)
void cgsleep_ms_r(cgtimer_t *ts_start, int ms);
void cgsleep_us_r(cgtimer_t *ts_start, int64_t us);
int cgtimer_to_ms(cgtimer_t *cgt);
void cgtimer_sub(cgtimer_t *a, cgtimer_t *b, cgtimer_t *res);
double us_tdiff(struct timeval *end, struct timeval *start);
int ms_tdiff(struct timeval *end, struct timeval *start);
double tdiff(struct timeval *end, struct timeval *start);
bool stratum_send(struct pool *pool, char *s, ssize_t len);
bool sock_full(struct pool *pool);
void ckrecalloc(void **ptr, size_t old, size_t new, const char *file, const char *func, const int line);
#define recalloc(ptr, old, new) ckrecalloc((void *)&(ptr), old, new, __FILE__, __func__, __LINE__)
char *recv_line(struct pool *pool);
bool parse_method(struct pool *pool, char *s);
bool extract_sockaddr(char *url, char **sockaddr_url, char **sockaddr_port);
bool auth_stratum(struct pool *pool);
bool initiate_stratum(struct pool *pool);
bool restart_stratum(struct pool *pool);
void suspend_stratum(struct pool *pool);
void dev_error(struct cgpu_info *dev, enum dev_reason reason);
void *realloc_strcat(char *ptr, char *s);
void *str_text(char *ptr);
void RenameThread(const char* name);
void _cgsem_init(cgsem_t *cgsem, const char *file, const char *func, const int line);
void _cgsem_post(cgsem_t *cgsem, const char *file, const char *func, const int line);
void _cgsem_wait(cgsem_t *cgsem, const char *file, const char *func, const int line);
int _cgsem_mswait(cgsem_t *cgsem, int ms, const char *file, const char *func, const int line);
void cgsem_reset(cgsem_t *cgsem);
void cgsem_destroy(cgsem_t *cgsem);
bool cg_completion_timeout(void *fn, void *fnarg, int timeout);
void _cg_memcpy(void *dest, const void *src, unsigned int n, const char *file, const char *func, const int line);

#define cgsem_init(_sem) _cgsem_init(_sem, __FILE__, __func__, __LINE__)
#define cgsem_post(_sem) _cgsem_post(_sem, __FILE__, __func__, __LINE__)
#define cgsem_wait(_sem) _cgsem_wait(_sem, __FILE__, __func__, __LINE__)
#define cgsem_mswait(_sem, _timeout) _cgsem_mswait(_sem, _timeout, __FILE__, __func__, __LINE__)
#define cg_memcpy(dest, src, n) _cg_memcpy(dest, src, n, __FILE__, __func__, __LINE__)

#endif /* __UTIL_H__ */
