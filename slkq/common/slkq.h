#ifndef _SLKQ_COMMON_H_
#define _SLKQ_COMMON_H_
#define DEBUG

#define SLKQ_NAME "slkq"
#define SLKQ_DEV "/dev/slkq"
#define SLKQ_SPOOL_FILENAME "/var/spool/slkq.dat"
#define SLKQ_PROC_STATUS_FILENAME "slkq_status"
#define SLKQ_MSG_MAX_SIZE (0x10000) /* 65536 */
#define SLKQ_READER_LOCK "/var/lock/slkq_reader.lock"
#define SLKQ_READER_OUTPUT_DIR "/var/cache/slkq"
#define SLKQ_READER_LOG "/var/log/slkq_reader.log"


#endif