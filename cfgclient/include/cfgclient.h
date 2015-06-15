#ifndef __CFG_CLIENT_H__
#define __CFG_CLIENT_H__

#ifdef __cplusplus
extern "C" {
#endif

enum CFG_PARTITION
{
	THIRDPARTY = 1,
	VMS,
	RESOURCE,
	ARTIFACT,
	SERVICE,
};

enum CFG_ERROR
{
	ERR_OK = 0,
	ERR_INVALID_HANDLE = -1,
	ERR_HANDLE_UNAVALIABLE = -2,
	ERR_ENQUEUE_NODE = -3,
	ERR_CREATE_NODE = -4,
	ERR_ZK_WGET_FAIL = -5,
	ERR_KEY_NOT_EXIST = -6,
	ERR_LOG_SET_FAIL = -7,
	ERR_PARAM_INVALID = -8,
	ERR_SYS_INTERNAL = -9,
	ERR_UNKNOWN = -10,
};

enum CFG_LOG_LEVEL
{
	CFG_LOG_LEVEL_ERROR=1,
	CFG_LOG_LEVEL_WARN=2,
	CFG_LOG_LEVEL_INFO=3,
	CFG_LOG_LEVEL_DEBUG=4
};

typedef struct _cfgclient cfgclient_t;
/**
 * 用户自定义watch回调函数
 */
typedef void (*cc_watcher_fn)(cfgclient_t *cc, 
		const char *key,
        const char *new_value, 
        int new_value_len, 
        void *watcherCtx);

/**
 * 设置配置中心api的日志参数
 * 日志级别(默认 CFG_LOG_LEVEL_INFO )，
 * 目录(默认程序执行当前目录)，
 * 文件名(默认 cfgclient.log)
 */
int cfg_set_log(enum CFG_LOG_LEVEL loglevel, const char *logdir, const char *logfile);

/**
 * 初始化配置中心客户端，若成功则返回非空客户端指针
 * 输入：需要连接的域名
 */
cfgclient_t * cfg_init(enum CFG_PARTITION domain);

/**
 * 获取配置数据
 * 输入：
 * cfgclient_t *cc  -- 配置中心客户端句柄(init返回的指针)
 * const char *key, -- 需要查询配置的key
 * 输出：
 * char *value,     -- 返回的数据buff指针
 * int *value_len   -- 返回数据的长度
 */
int cfg_get_data(cfgclient_t *cc, const char *key, char *value, int *value_len);

/**
 * 设置监听
 * 返回 0 success 否则错误
 * 输入：
 * cfgclient_t *cc  	-- 配置中心客户端句柄(init返回的指针)
 * const char *key, 	-- 需要监听配置的key
 * cc_watcher_fn watcher,     -- 监听函数地址
 * void *watcherCtx   	-- 透传的用户侧上下文指针，作为监听函数的输入
 * 输出：
 * char *out_value,     -- 返回的数据buff指针
 * int *out_value_len   -- 返回数据的长度
 */
int cfg_set_watcher(cfgclient_t *cc, const char *key, cc_watcher_fn watcher, void *watcherCtx, char *out_value, int *out_value_len);

/**
 * 去除监听
 * 返回 0 success 否则错误
 * 输入：
 * cfgclient_t *cc  -- 配置中心客户端句柄(init返回的指针)
 * const char *key, -- 需要去除监听配置的key
 */
int cfg_clear_watcher(cfgclient_t *cc, const char *key);

/**
 * 销毁配置中心客户端句柄
 * 返回 0 success 否则错误
 * 输入：
 * cfgclient_t *cc  -- 配置中心客户端句柄(init返回的指针)
 */
int cfg_destroy(cfgclient_t *cc);

#ifdef __cplusplus
}
#endif

#endif // __CFG_CLIENT_H__
