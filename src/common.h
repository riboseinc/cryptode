#ifndef __RVD_COMMON_H__
#define __RVD_COMMON_H__

/* macro constants */

#define RVD_MAX_PATH				256
#define RVD_MAX_LINE				512
#define RVD_MAX_LOGMSG_LEN			1024
#define RVD_MAX_CMD_LEN				2048
#define RVD_MAX_RESP_LEN			2048
#define RVD_MAX_CONN_NAME_LEN			128
#define RVD_MAX_CONN_INFO_LEN			2048

/* rvd default configuration path */
#define RVD_DEFAULT_CONFIG_PATH			"/opt/rvc/etc/rvd.json"

/* rvd default command listen socket path */
#define RVD_DEFAULT_LISTEN_SOCK			"/tmp/.rvd_cmd"

/* rvd default log file path */
#define RVD_DEFAULT_LOG_PATH			"/var/log/rvd.log"

/* rvd default UID for writting to rvd socket */
#define RVD_DEFAULT_UID				501


/* rvd command codes */
enum RVD_CMD_CODE {
	RVD_CMD_LIST = 0,
	RVD_CMD_CONNECT,
	RVD_CMD_DISCONNECT,
	RVD_CMD_STATUS,
	RVD_CMD_SCRIPT_SECURITY,
	RVD_CMD_UNKNOWN
};

/* rvd command response codes */
enum RVD_RESP_CODE
{
	RVD_RESP_OK = 0,
	RVD_RESP_INVALID_CMD,
	RVD_RESP_NO_MEMORY,
	RVD_RESP_EMPTY_LIST,
	RVD_RESP_CONN_NOT_FOUND,
	RVD_RESP_CONN_ALREADY_CONNECTED,
	RVD_RESP_CONN_ALREADY_DISCONNECTED,
	RVD_RESP_CONN_IN_PROGRESS,
};

#endif /* __RVD_COMMON_H__ */