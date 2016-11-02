#ifndef __CATALOG__
#define __CATALOG__

#define CATALOG_MAGIC 0x1000

#define SQL_ARCHIVE_ID_ATTNO 0
#define SQL_ARCHIVE_NAME_ATTNO 1
#define SQL_ARCHIVE_DIRECTORY_ATTNO 2
#define SQL_ARCHIVE_COMPRESSION_ATTNO 3
#define SQL_ARCHIVE_PGHOST_ATTNO 4
#define SQL_ARCHIVE_PGPORT_ATTNO 5
#define SQL_ARCHIVE_PGUSER_ATTNO 6
#define SQL_ARCHIVE_PGDATABASE_ATTNO 7

#define SQL_BACKUP_ID_ATTNO 0
#define SQL_BACKUP_ARCHIVE_ID_ATTNO 1
#define SQL_BACKUP_HISTORY_FILENAME_ATTNO 2
#define SQL_BACKUP_LABEL_ATTNO 3
#define SQL_BACKUP_STARTED_ATTNO 4
#define SQL_BACKUP_STOPPED_ATTNO 5
#define SQL_BACKUP_PINNED_ATTNO = 6

#endif
