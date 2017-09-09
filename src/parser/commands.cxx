#include <boost/format.hpp>
#include <commands.hxx>
#include <daemon.hxx>
#include <stream.hxx>

using namespace credativ;

BaseCatalogCommand::~BaseCatalogCommand() {}

void BaseCatalogCommand::copy(CatalogDescr& source) {

  this->tag = source.tag;
  this->id  = source.id;
  this->archive_name = source.archive_name;
  this->label        = source.label;
  this->compression  = source.compression;
  this->directory    = source.directory;

  /*
   * Connection properties
   */
  this->coninfo->type         = source.coninfo->type;
  this->coninfo->pghost       = source.coninfo->pghost;
  this->coninfo->pgport       = source.coninfo->pgport;
  this->coninfo->pguser       = source.coninfo->pguser;
  this->coninfo->pgdatabase   = source.coninfo->pgdatabase;
  this->coninfo->dsn          = source.coninfo->dsn;
  this->backup_profile = source.getBackupProfileDescr();

  /*
   * Job control properties
   */
  this->detach = source.detach;

  this->setAffectedAttributes(source.getAffectedAttributes());
  this->coninfo->setAffectedAttributes(source.coninfo->getAffectedAttributes());

}

DropConnectionCatalogCommand::DropConnectionCatalogCommand(std::shared_ptr<CatalogDescr> descr) {

  this->copy(*(descr.get()));

}

DropConnectionCatalogCommand::DropConnectionCatalogCommand(std::shared_ptr<BackupCatalog> catalog) {
  this->setCommandTag(tag);
  this->catalog = catalog;
}

void
DropConnectionCatalogCommand::execute(bool flag) {

  if (this->catalog == nullptr) {
    throw CArchiveIssue("could not execute drop command: no catalog");
  }

  if (!this->catalog->available()) {
    this->catalog->open_rw();
  }

  /*
   * Start transaction, this will lock the
   * database during this operation.
   */
  this->catalog->startTransaction();

  try {

    std::shared_ptr<CatalogDescr> tempDescr;
    string contype;

    /*
     * Checkout if our archive_name really exists.
     */
    tempDescr = this->catalog->existsByName(this->archive_name);

    /*
     * BackupCatalog::existsByName() returns an empty
     * catalog descriptor.
     */
    if (tempDescr != nullptr && tempDescr->id >= 0) {

      /*
       * Archive exists, drop its streaming connection, but
       * check if the requested connection really exists.
       *
       * XXX: the parser has initialized our
       *      connection descr handle accordingly.
       */

      /* ... we're interested in archive_id only. */
      this->setArchiveId(tempDescr->id);
      this->coninfo->pushAffectedAttribute(SQL_CON_ARCHIVE_ID_ATTNO);

      /* save the connection type for error reporting later */
      contype = this->coninfo->type;

      this->catalog->getCatalogConnection(this->coninfo,
                                          this->id,
                                          this->coninfo->type);

      if (this->coninfo->archive_id < 0) {
        throw CCatalogIssue("archive \""
                            + this->archive_name
                            + "\" does not have a connection of type \""
                            + contype
                            +"\"");
      }

      this->catalog->dropCatalogConnection(this->archive_name,
                                           this->coninfo->type);

    } else {
      /* requested archive name is unknown */
      throw CCatalogIssue("archive \"" + this->archive_name + "\" does not exist");
    }

  } catch (CPGBackupCtlFailure& e) {
    this->catalog->rollbackTransaction();

    /* re-throw exception to caller */
    throw e;
  }

  /* ... and we're done */
  this->catalog->commitTransaction();
}

ListConnectionCatalogCommand::ListConnectionCatalogCommand(std::shared_ptr<CatalogDescr> descr) {

  this->copy(*(descr.get()));

}

ListConnectionCatalogCommand::ListConnectionCatalogCommand(std::shared_ptr<BackupCatalog> catalog) {
  this->setCommandTag(tag);
  this->catalog = catalog;
}

void ListConnectionCatalogCommand::execute(bool flag) {

  if (this->catalog == nullptr) {
    throw CArchiveIssue("could not execute list command: no catalog");
  }

  /*
   * Prepare catalog, start transaction ...
   */
  if (!this->catalog->available()) {
    this->catalog->open_rw();
  }

  this->catalog->startTransaction();

  try {

    std::shared_ptr<CatalogDescr> tempDescr;
    std::vector<std::shared_ptr<ConnectionDescr>> connections;

    /*
     * Check if specified archive is valid.
     */
    tempDescr = this->catalog->existsByName(this->archive_name);

    /*
     * Normally we don't get a nullptr back from
     * existsByName() ...
     */
    if (tempDescr != nullptr
        && tempDescr->id >= 0) {

      /*
       * Archive exists and existsByName() has initialized
       * our temporary descriptor with its archive_id we need to
       * retrieve associated catalog database connections.
       */
      connections = this->catalog->getCatalogConnection(tempDescr->id);

      /*
       * Print result header
       */
      cout << "List of connections for archive \""
           << this->archive_name
           << "\""
           << endl;

      /*
       * XXX: createArchive() normally ensures that a
       *      catalog connection definition of type 'basebackup'
       *      (CONNECTION_TYPE_BASEBACKUP) exists, at least. But
       *      we don't rely on this fact, just loop through
       *      the results and spill them out...
       *
       *      getCatalogConnection() returns the shared pointers
       *      ordered by its type.
       */
      for(auto & con : connections) {

        /* item header */
        cout << CPGBackupCtlBase::makeHeader("connection type " + con->type,
                                             boost::format("%-15s\t%-60s") % "Attribute" % "Setting",
                                             80);
        cout << boost::format("%-15s\t%-60s") % "DSN" % con->dsn << endl;
        cout << boost::format("%-15s\t%-60s") % "PGHOST" % con->pghost << endl;
        cout << boost::format("%-15s\t%-60s") % "PGDATABASE" % con->pgdatabase << endl;
        cout << boost::format("%-15s\t%-60s") % "PGUSER" % con->pguser << endl;
        cout << boost::format("%-15s\t%-60s") % "PGPORT" % con->pgport << endl;
      }
    } else {

      ostringstream oss;
      oss << "archive \"" << this->archive_name << "\" does not exist";
      throw CCatalogIssue(oss.str());

    }

  } catch(CPGBackupCtlFailure& e) {

    this->catalog->rollbackTransaction();

    /* re-throw to caller */
    throw e;

  }

  /* ... and we're done */
  this->catalog->commitTransaction();
}

CreateConnectionCatalogCommand::CreateConnectionCatalogCommand(std::shared_ptr<BackupCatalog> catalog) {
  this->setCommandTag(CREATE_CONNECTION);
  this->catalog = catalog;
}

CreateConnectionCatalogCommand::CreateConnectionCatalogCommand(std::shared_ptr<CatalogDescr> descr) {

  this->copy(*(descr.get()));

}

void CreateConnectionCatalogCommand::execute(bool flag) {

  if (this->catalog == nullptr) {
    throw CArchiveIssue("could not execute create connection command: no catalog");
  }

  /*
   * Prepare catalog, start transaction...
   */
  if (!this->catalog->available()) {
    this->catalog->open_rw();
  }

  this->catalog->startTransaction();

  try {

    /*
     * Check if the requested archive really exists.
     */
    std::shared_ptr<CatalogDescr> tempArchiveDescr = nullptr;
    shared_ptr<ConnectionDescr> tempConDescr = std::make_shared<ConnectionDescr>();

    tempArchiveDescr = this->catalog->existsByName(this->archive_name);

    if (tempArchiveDescr->id < 0) {
      std::ostringstream oss;
      /* Requested archive doesn't exist */
      oss << "archive \""
          << this->archive_name
          << "\" does not exist";
      throw CCatalogIssue(oss.str());
    }

    /*
     * Archive exists, proceed.
     *
     * We need to take care to assign the archive_id to our
     * own object instance, since the API currently doesn't
     * support direct archive_name lookups during connection
     * creation. This also makes sure, that the current command
     * has a valid archive identification.
     */
    this->setArchiveId(tempArchiveDescr->id);

    /*
     * Check, if there is already a connection type colliding
     * with the new one.
     */
    tempConDescr->pushAffectedAttribute(SQL_CON_ARCHIVE_ID_ATTNO);
    tempConDescr->pushAffectedAttribute(SQL_CON_TYPE_ATTNO);
    this->catalog->getCatalogConnection(tempConDescr,
                                        tempArchiveDescr->id,
                                        this->coninfo->type);

    if (tempConDescr->archive_id >= 0
        && tempConDescr->type != ConnectionDescr::CONNECTION_TYPE_UNKNOWN) {

      /*
       * This connection type already has a definition
       * for the requested archive, error out.
       */
      std::ostringstream oss;
      oss << "archive \""
          << this->archive_name
          << "\" already has a connection of this type configured"
          << endl;
      throw CCatalogIssue(oss.str());
    }

    /*
     * Everything looks good; create the new connection.
     */
    this->catalog->createCatalogConnection(this->coninfo);

    /* ... and we're done, commit transaction */
    this->catalog->commitTransaction();

  } catch(CPGBackupCtlFailure& e) {

    this->catalog->rollbackTransaction();

    /* re-raise */
    throw e;

  }
}

StartLauncherCatalogCommand::StartLauncherCatalogCommand(std::shared_ptr<BackupCatalog> catalog) {
  this->tag = START_LAUNCHER;
  this->catalog = catalog;
}

StartLauncherCatalogCommand::StartLauncherCatalogCommand(std::shared_ptr<CatalogDescr> descr) {

  this->copy(*(descr.get()));

}

void StartLauncherCatalogCommand::execute(bool flag) {

  job_info job_info;
  pid_t pid;

  if (this->catalog == nullptr) {
    throw CArchiveIssue("could not execute catalog command: no catalog");
  }

  /*
   * Detach from current interactive terminal, if requested (default).
   */
  job_info.detach = this->detach;

  /*
   * Close standard file descriptors (STDOUT, STDIN, STDERR, ...), otherwise
   * background process will clobber our interactive terminal.
   */
  job_info.close_std_fd = false;

  /*
   * Assign catalog descriptor handle to the job info.
   */
  job_info.cmdHandle = std::make_shared<BackgroundWorkerCommandHandle>(this->catalog);

  /*
   * Finally launch the background worker.
   */
  pid = launch(job_info);


  if (pid > 0)
    cout << "background launcher launched at pid " << pid << endl;
  else {
    /*
     * This code shouldn't be reached, since the forked child process
     * should take care to exit in their child code after executing
     * the background action.
     */
    exit(0);
  }
}

ListBackupCatalogCommand::ListBackupCatalogCommand(std::shared_ptr<BackupCatalog> catalog) {
  this->tag = LIST_BACKUP_CATALOG;
  this->catalog = catalog;
}

ListBackupCatalogCommand::ListBackupCatalogCommand(std::shared_ptr<CatalogDescr> descr) {

  this->copy(*(descr.get()));

}

void ListBackupCatalogCommand::execute(bool flag) {

  /*
   * Die hard in case no catalog available.
   */
  if (this->catalog == nullptr)
    throw CArchiveIssue("could not execute catalog command: no catalog");

  if (!this->catalog->available())
    this->catalog->open_rw();

  /*
   * Enter transaction
   */
  try {

    std::shared_ptr<StatCatalog> stat = nullptr;
    std::shared_ptr<CatalogDescr> temp_descr = nullptr;

    this->catalog->startTransaction();

    /*
     * Check if requested archive exists in the catalog.
     */
    temp_descr = this->catalog->existsByName(this->archive_name);

    if (temp_descr->id < 0) {
      /*
       * Don't need to rollback, outer exception handler will do this
       */
      std::ostringstream oss;
      oss << "cannot stat catalog: archive\""
          << this->archive_name
          << "\" does not exist";

      throw CCatalogIssue(oss.str());
    }
    /* list of all backups in every archive */
    stat = this->catalog->statCatalog(this->archive_name);
    cout << stat->gimmeFormattedString();

    this->catalog->commitTransaction();

  } catch (CPGBackupCtlFailure &e) {
    this->catalog->rollbackTransaction();
    throw e; /* don't hide exception from caller */
  }
}

StartBasebackupCatalogCommand::StartBasebackupCatalogCommand(std::shared_ptr<BackupCatalog> catalog) {
  this->tag = START_BASEBACKUP;
  this->catalog = catalog;
}

StartBasebackupCatalogCommand::StartBasebackupCatalogCommand(std::shared_ptr<CatalogDescr> descr) {

  this->copy(*(descr.get()));

}

void StartBasebackupCatalogCommand::execute(bool background) {

  std::shared_ptr<CatalogDescr> temp_descr(nullptr);
  std::shared_ptr<BackupProfileDescr> backupProfile(nullptr);

  /*
   * Basebackup stream process handler.
   */
  std::shared_ptr<BaseBackupProcess> bbp(nullptr);

  /* Track if basebackup was registered already */
  bool basebackup_registered = false;

  /*
   * Die hard in case no catalog descriptor available.
   */
  if (this->catalog == nullptr)
    throw CArchiveIssue("could not execute archive command: no catalog");

  /*
   * Open the catalog, if not done yet.
   */
  if (!this->catalog->available()) {
    this->catalog->open_rw();
  }

  this->catalog->startTransaction();

  try {
    /*
     * Check if the specified archive_name is present, get
     * its descriptor.
     */
    temp_descr = this->catalog->existsByName(this->archive_name);

    /*
     * existsByName() doesn't retrieve the archive
     * database connection specification into our
     * catalog descriptor, we need to do it ourselves.
     */
    if (temp_descr->id >= 0) {

      temp_descr->coninfo->pushAffectedAttribute(SQL_CON_ARCHIVE_ID_ATTNO);
      temp_descr->coninfo->pushAffectedAttribute(SQL_CON_TYPE_ATTNO);
      temp_descr->coninfo->pushAffectedAttribute(SQL_CON_DSN_ATTNO);
      temp_descr->coninfo->pushAffectedAttribute(SQL_CON_PGHOST_ATTNO);
      temp_descr->coninfo->pushAffectedAttribute(SQL_CON_PGPORT_ATTNO);
      temp_descr->coninfo->pushAffectedAttribute(SQL_CON_PGUSER_ATTNO);
      temp_descr->coninfo->pushAffectedAttribute(SQL_CON_PGDATABASE_ATTNO);

      this->catalog->getCatalogConnection(temp_descr->coninfo,
                                          temp_descr->id,
                                          ConnectionDescr::CONNECTION_TYPE_BASEBACKUP);
    }
    this->catalog->commitTransaction();

  } catch(CPGBackupCtlFailure& e) {
    /* oops */
    this->catalog->rollbackTransaction();
    throw e;
  }

  if (temp_descr->id < 0) {
    /* Requested archive doesn't exist, error out */
    std::ostringstream oss;

    oss << "archive " << this->archive_name << " doesn't exist";
    this->catalog->rollbackTransaction();
    throw CArchiveIssue(oss.str());
  }

  /*
   * If the PROFILE keyword was specified, select
   * the requested profile from the catalog. If it doesn't
   * exist, throw an error.
   *
   * Iff PROFILE was omitted, select the default profile.
   */
  if (this->backup_profile->name != "") {

#ifdef __DEBUG__
    cerr << "DEBUG: checking for profile " << this->backup_profile->name << endl;
#endif

    this->catalog->startTransaction();

    try {
      backupProfile = catalog->getBackupProfile(this->backup_profile->name);
      this->catalog->commitTransaction();
    } catch(CPGBackupCtlFailure &e) {
      this->catalog->rollbackTransaction();
      throw e;
    }

    /*
     * If the requested backup profile was not found, raise
     * an exception.
     */
    if (backupProfile->profile_id < 0) {
      std::ostringstream oss;
      oss << "backup profile \"" << this->backup_profile->name << "\" does not exist";
      throw CArchiveIssue(oss.str());
    }

  } else {

    this->catalog->startTransaction();

    try {

      /*
       * The PROFILE keyword wasn't specified to
       * the START BASEBACKUP command. Request the default
       * profile...
       */
      backupProfile = catalog->getBackupProfile("default");
      this->catalog->commitTransaction();

    } catch(CPGBackupCtlFailure &e) {
      this->catalog->rollbackTransaction();
      throw e;
    }

#ifdef __DEBUG__
    cerr << "PROFILE keyword not specified, using \"default\" backup profile" << endl;
#endif

    /*
     * Iff the "default" profile doesn't exist, tell
     * the user that this is an unexpected error condition.
     */
    if (backupProfile->profile_id < 0) {
      std::ostringstream oss;
      oss << "\"default\" profile not found: please check your backup catalog or create a new one";
      throw CArchiveIssue(oss.str());
    }

  }

  try {

    PGStream pgstream(temp_descr);

    /*
     * Create base backup stream handler.
     */
    std::shared_ptr<StreamBaseBackup> backupHandle
      = std::make_shared<StreamBaseBackup>(temp_descr);

    /*
     * Meta information handle for streamed tablespace.
     */
    std::shared_ptr<BackupTablespaceDescr> tablespaceDescr = nullptr;

    std::shared_ptr<BaseBackupDescr> basebackupDescr = nullptr;

    /*
     * Backup profile tells us the compression mode to use...
     */
    backupHandle->setCompression(backupProfile->compress_type);

    /*
     * Get connection to the PostgreSQL instance.
     */
    pgstream.connect();

#ifdef __DEBUG__
    cerr << "DEBUG: connecting stream" << endl;
#endif

    /*
     * Identify this replication connection.
     */
    pgstream.identify();

#ifdef __DEBUG__
    cerr << "DEBUG: identify stream" << endl;
#endif

    /*
     * Get basebackup stream handle.
     */
    bbp = pgstream.basebackup(backupProfile);

#ifdef __DEBUG__
    cerr << "DEBUG: basebackup stream handle initialize" << endl;
#endif

    /*
     * Enter basebackup stream.
     */
    bbp->start();

    /*
     * Now its time to register this basebackup handle to
     * the catalog. Do the rollback in our own exception handler
     * in case of errors.
     *
     * NOTE: Since the streaming directory handle is just passed to
     *       stepTablespace(), but we need to register the basebackup
     *       before, we must set the fsentry to the basebackup descriptor
     *       ourselves!
     */
    basebackupDescr = bbp->getBaseBackupDescr();

    this->catalog->startTransaction();

    try {

      /*
       * Prepare backup handler. Should successfully create
       * target streaming directory...
       */
      backupHandle->initialize();
      backupHandle->create();

      basebackupDescr->archive_id = temp_descr->id;
      basebackupDescr->fsentry = backupHandle->backupDirectoryString();

      cerr << "directory handle path " << basebackupDescr->fsentry << endl;

      this->catalog->registerBasebackup(temp_descr->id,
                                        basebackupDescr);
      this->catalog->commitTransaction();

    } catch(CPGBackupCtlFailure &e) {
      this->catalog->rollbackTransaction();
      throw e;
    }

    /*
     * Remember successful registration of this basebackup.
     */
    basebackup_registered = true;

#ifdef __DEBUG__
    cerr << "DEBUG: basebackup stream started" << endl;
#endif

    /*
     * Initialize list of tablespaces, if any.
     */
    bbp->readTablespaceInfo();

#ifdef __DEBUG__
    cerr << "DEBUG: basebackup tablespace meta info requested" << endl;
#endif

    /*
     * Loop through tablespaces and stream their contents.
     */
    while(bbp->stepTablespace(backupHandle,
                              tablespaceDescr)) {
#ifdef __DEBUG__
      cerr << "streaming tablespace OID "
           << tablespaceDescr->spcoid
           << ",size " << tablespaceDescr->spcsize
           << endl;
#endif
      /*
       * Since we register each backup tablespace, we need
       * to record the backup id, it belongs to.
       */
      tablespaceDescr->backup_id = basebackupDescr->id;
      catalog->registerTablespaceForBackup(tablespaceDescr);
      bbp->backupTablespace(tablespaceDescr);
    }

    /*
     * Call end position in backup stream.
     */
    bbp->end();

    /*
     * And disconnect
     */
#ifdef __DEBUG__
    cerr << "DEBUG: disconnecting stream" << endl;
#endif

    pgstream.disconnect();
  } catch(CPGBackupCtlFailure& e) {

    bool txinprogress = false;

    /*
     * If the basebackup was already registered, mark it
     * as aborted. This is sad even if all went through, but only
     * disconnecting from the server throws an error, but we treat this
     * as an error nevertheless. We must be careful here, since catalog
     * operations itself can throw exceptions and we don't mask
     * those errors with the current one.
     */
    try {
      if (basebackup_registered) {
        this->catalog->startTransaction();
        txinprogress = true;

#ifdef __DEBUG__
        cerr << "DEBUG: marking basebackup as aborted" << endl;
#endif

        this->catalog->abortBasebackup(bbp->getBaseBackupDescr());
        this->catalog->commitTransaction();
      }
    } catch (CPGBackupCtlFailure &e) {
      if (txinprogress)
        this->catalog->rollbackTransaction();
    }

    /* re-throw ... */
    throw e;

  }

  /*
   * Everything seems okay for now, finalize the backup
   * registration.
   */
  this->catalog->startTransaction();

  try {
    this->catalog->finalizeBasebackup(bbp->getBaseBackupDescr());
    this->catalog->commitTransaction();
  } catch (CPGBackupCtlFailure &e) {
    this->catalog->rollbackTransaction();
    throw e;
  }

}

DropBackupProfileCatalogCommand::DropBackupProfileCatalogCommand(std::shared_ptr<BackupCatalog> catalog) {
  this->tag = DROP_BACKUP_PROFILE;
  this->catalog = catalog;
}

DropBackupProfileCatalogCommand::DropBackupProfileCatalogCommand(std::shared_ptr<CatalogDescr> descr) {

  this->copy(*(descr.get()));

}

void DropBackupProfileCatalogCommand::execute(bool extended) {

  /*
   * Die hard in case no catalog available.
   */
  if (this->catalog == nullptr)
    throw CArchiveIssue("could not execute archive command: no catalog");

  /*
   * Open the catalog, if not yet done.
   */
  if (!this->catalog->available()) {
    this->catalog->open_rw();
  }

  try {

    catalog->startTransaction();

    /*
     * Check if the specified backup profile exists. Error
     * out with an exception if not found.
     */
    std::shared_ptr<BackupProfileDescr> profileDescr = this->getBackupProfileDescr();
    std::shared_ptr<BackupProfileDescr> temp_descr = catalog->getBackupProfile(profileDescr->name);

    /*
     * NOTE: BackupCatalog::getBackupProfile() always returns an
     *       BackupProfileDescr, but in case the specified
     *       name doesn't exists it is initialized with profile_id = -1
     */
    if (temp_descr->profile_id < 0) {
      std::ostringstream oss;
      oss << "backup profile \"" << profileDescr->name << "\"" << endl;
      throw CCatalogIssue(oss.str());
    }

    catalog->dropBackupProfile(profileDescr->name);
    catalog->commitTransaction();

  } catch(exception& e) {
    catalog->rollbackTransaction();
    /* re-throw exception */
    throw e;
  }
}

ListBackupProfileCatalogCommand::ListBackupProfileCatalogCommand(std::shared_ptr<BackupCatalog> catalog) {
  this->tag = LIST_BACKUP_PROFILE;
  this->catalog = catalog;
}

ListBackupProfileCatalogCommand::ListBackupProfileCatalogCommand(std::shared_ptr<CatalogDescr> descr) {

  this->copy(*(descr.get()));

}

void ListBackupProfileCatalogCommand::execute(bool extended) {

  shared_ptr<CatalogDescr> temp_descr(nullptr);

  /*
   * Die hard in case no catalog available.
   */
  if (this->catalog == nullptr)
    throw CArchiveIssue("could not execute archive command: no catalog");

  /*
   * Open the catalog, if not yet done.
   */
  if (!this->catalog->available()) {
    this->catalog->open_rw();
  }

  try {

    catalog->startTransaction();

    /*
     * Get a list of current backup profiles. If the name is requested,
     * show the details only.
     */
    if (this->tag == LIST_BACKUP_PROFILE) {

      auto profileList = catalog->getBackupProfiles();

      /*
       * Print headline
       */
      cout << CPGBackupCtlBase::makeHeader("List of backup profiles",
                                           boost::format("%-25s\t%-15s")
                                           % "Name" % "Backup Label", 80);

      /*
       * Print name and label
       */
      for (auto& descr : *profileList) {
        cout << boost::format("%-25s\t%-15s") % descr->name % descr->label
             << endl;
      }

    } else if (this->tag == LIST_BACKUP_PROFILE_DETAIL) {

      std::shared_ptr<BackupProfileDescr> profile
        = this->catalog->getBackupProfile(this->getBackupProfileDescr()->name);

      cout << CPGBackupCtlBase::makeHeader("Details for backup profile " + profile->name,
                                           boost::format("%-25s\t%-40s") % "Property" % "Setting", 80);

      /* Profile Name */
      cout << boost::format("%-25s\t%-30s") % "NAME" % profile->name << endl;

      /* Profile compression type */
      switch(profile->compress_type) {
      case BACKUP_COMPRESS_TYPE_NONE:
        cout << boost::format("%-25s\t%-30s") % "COMPRESSION" % "NONE" << endl;
        break;
      case BACKUP_COMPRESS_TYPE_GZIP:
        cout << boost::format("%-25s\t%-30s") % "COMPRESSION" % "GZIP" << endl;
        break;
      case BACKUP_COMPRESS_TYPE_ZSTD:
        cout << boost::format("%-25s\t%-30s") % "COMPRESSION" % "ZSTD" << endl;
        break;
      default:
        cout << boost::format("%-25s\t%-30s") % "COMPRESSION" % "UNKNOWN or N/A" << endl;
        break;
      }

      /* Profile max rate */
      if (profile->max_rate > 0) {
        cout << boost::format("%-25s\t%-30s") % "MAX RATE" % "NOT RATED" << endl;
      } else {
        cout << boost::format("%-25s\t%-30s") % "MAX RATE(kbps)" % profile->max_rate << endl;
      }

      /* Profile backup label */
      cout << boost::format("%-25s\t%-30s") % "LABEL" % profile->label << endl;

      /* Profile fast checkpoint mode */
      cout << boost::format("%-25s\t%-30s") % "FAST CHECKPOINT" % profile->fast_checkpoint << endl;

      /* Profile WAL included */
      cout << boost::format("%-25s\t%-30s") % "WAL INCLUDED" % profile->include_wal << endl;

      /* Profile WAIT FOR WAL */
      cout << boost::format("%-25s\t%-30s") % "WAIT FOR WAL" % profile->wait_for_wal << endl;
    }

    catalog->commitTransaction();

  } catch (exception& e) {
    this->catalog->rollbackTransaction();
    throw e;
  }

}

CreateBackupProfileCatalogCommand::CreateBackupProfileCatalogCommand(std::shared_ptr<CatalogDescr> descr) {

  this->copy(*(descr.get()));

  /*
   * BaseCatalogCommand::copy() has no idea of submodule objects belongig
   * to a given catalog descriptor. We need to copy them explicitely.
   */
  this->profileDescr = descr->getBackupProfileDescr();

}

CreateBackupProfileCatalogCommand::CreateBackupProfileCatalogCommand(std::shared_ptr<BackupCatalog> catalog) {
  this->tag = CREATE_BACKUP_PROFILE;
  this->catalog = catalog;
}

CreateBackupProfileCatalogCommand::CreateBackupProfileCatalogCommand() {
  this->tag = CREATE_BACKUP_PROFILE;
}

void CreateBackupProfileCatalogCommand::setProfile(std::shared_ptr<BackupProfileDescr> profileDescr) {
  this->profileDescr = profileDescr;
}

void CreateBackupProfileCatalogCommand::execute(bool existsOk) {

  if (this->catalog == nullptr)
    throw CArchiveIssue("could not execute archive command: no catalog");

#ifdef __DEBUG__
  cout << "name: " << this->profileDescr->name << endl;
  cout << "compression: " << this->profileDescr->compress_type << endl;
  cout << "max rate: " << this->profileDescr->max_rate << endl;
  cout << "label: " << this->profileDescr->label << endl;
  cout << "wal included: " << this->profileDescr->include_wal << endl;
  cout << "wait for wal: " << this->profileDescr->wait_for_wal << endl;
#endif

  /*
   * Open the catalog, if necessary.
   */
  if (!this->catalog->available()) {
    this->catalog->open_rw();
  }

  try {

    /*
     * Start catalog transaction.
     */
    this->catalog->startTransaction();

    /*
     * Check if the specified backup profile already exists
     */
    std::shared_ptr<BackupProfileDescr> temp_descr = catalog->getBackupProfile(this->profileDescr->name);

    if (temp_descr->profile_id < 0) {

      /*
       * Create the new profile.
       *
       * NOTE: we can't bindly use the new BackupProfileDescr passed down
       *       from the parser, since it might not have seen all
       *       the attributes, the user might decided not to overwrite its default.
       *       Hence, pass down all attributes which are required to create
       *       a new entry.
       */

      std::vector<int> attr;
      attr.push_back(SQL_BCK_PROF_NAME_ATTNO);
      attr.push_back(SQL_BCK_PROF_COMPRESS_TYPE_ATTNO);
      attr.push_back(SQL_BCK_PROF_MAX_RATE_ATTNO);
      attr.push_back(SQL_BCK_PROF_LABEL_ATTNO);
      attr.push_back(SQL_BCK_PROF_FAST_CHKPT_ATTNO);
      attr.push_back(SQL_BCK_PROF_INCL_WAL_ATTNO);
      attr.push_back(SQL_BCK_PROF_WAIT_FOR_WAL_ATTNO);

      this->profileDescr->setAffectedAttributes(attr);
      this->catalog->createBackupProfile(this->profileDescr);

    } else {

      /* profile already exists! */
      if (!existsOk) {
        ostringstream oss;
        oss << "backup profile " << this->profileDescr->name << " already exists";
        throw CCatalogIssue(oss.str());
      }

    }

    this->catalog->commitTransaction();

  } catch(CPGBackupCtlFailure& e) {
    this->catalog->rollbackTransaction();
    throw e;
  }

}

VerifyArchiveCatalogCommand::VerifyArchiveCatalogCommand(std::shared_ptr<CatalogDescr> descr) {

  this->copy(*(descr.get()));

}

VerifyArchiveCatalogCommand::VerifyArchiveCatalogCommand(std::shared_ptr<BackupCatalog> catalog) {
  this->tag = VERIFY_ARCHIVE;
  this->catalog = catalog;
}

VerifyArchiveCatalogCommand::VerifyArchiveCatalogCommand() {
  this->tag = VERIFY_ARCHIVE;
}

void VerifyArchiveCatalogCommand::execute(bool missingOK) {

  /*
   * Check if the specified archive really exists.
   */
  shared_ptr<CatalogDescr> temp_descr(nullptr);

  /*
   * Die hard in case no catalog available.
   */
  if (this->catalog == nullptr)
    throw CArchiveIssue("could not execute archive command: no catalog");

  /*
   * Open the catalog, if not yet done.
   */
  if (!this->catalog->available()) {
    this->catalog->open_rw();
  }

  try {

    catalog->startTransaction();

    temp_descr = catalog->existsByName(this->archive_name);

    if (temp_descr->id < 0) {
      ostringstream oss;
      oss << "archive " << this->archive_name << " does not exist";
      throw CCatalogIssue(oss.str());
    }

    /*
     * There is an archive with the specified identifier registered.
     * Get an archive directory handle and check wether its directory structure
     * is intact.
     */
    shared_ptr<BackupDirectory> archivedir = CPGBackupCtlFS::getArchiveDirectoryDescr(temp_descr->directory);
    archivedir->verify();

    this->catalog->commitTransaction();

  } catch(CPGBackupCtlFailure& e) {

    this->catalog->rollbackTransaction();
    throw e;

  }

}

ListArchiveCatalogCommand::ListArchiveCatalogCommand(std::shared_ptr<CatalogDescr> descr) {

  this->copy(*(descr.get()));

}

ListArchiveCatalogCommand::ListArchiveCatalogCommand(std::shared_ptr<BackupCatalog> catalog) {
  this->tag = LIST_ARCHIVE;
  this->catalog = catalog;
}

ListArchiveCatalogCommand::ListArchiveCatalogCommand() {
  this->tag = LIST_ARCHIVE;
}

void ListArchiveCatalogCommand::execute(bool extendedOutput) {

  shared_ptr<CatalogDescr> temp_descr(nullptr);

  /*
   * Die hard in case no catalog available.
   */
  if (this->catalog == nullptr)
    throw CArchiveIssue("could not execute archive command: no catalog");

  /*
   * Open the catalog, if not yet done.
   */
  if (!this->catalog->available()) {
    this->catalog->open_rw();
  }

  try {
    catalog->startTransaction();

    if (this->mode == ARCHIVE_LIST) {

      /*
       * Get a list of current registered archives in the catalog.
       */
      auto archiveList = catalog->getArchiveList();

      /*
       * Print headline
       */
      cout << CPGBackupCtlBase::makeHeader("List of archives",
                                           boost::format("%-15s\t%-30s") % "Name" % "Directory",
                                           80);

      /*
       * Print archive properties
       */
      for (auto& descr : *archiveList) {
        cout << CPGBackupCtlBase::makeLine(boost::format("%-15s\t%-30s")
                                           % descr->archive_name
                                           % descr->directory);
      }
    }
    else if (this->mode == ARCHIVE_FILTERED_LIST) {

      /*
       * Get a filtered list for the specified properties.
       */
      auto archiveList = catalog->getArchiveList(make_shared<CatalogDescr>(*this),
                                                 this->affectedAttributes);

      /* Print headline */
      cout << CPGBackupCtlBase::makeHeader("Filtered archive list",
                                           boost::format("%-15s\t%-30s") % "Name" % "Directory",
                                           80);

      /*
       * Print archive properties
       */
      for (auto& descr : *archiveList) {
        cout << CPGBackupCtlBase::makeLine(boost::format("%-15s\t%-30s")
                                           % descr->archive_name
                                           % descr->directory);
      }

    } else if (this->mode == ARCHIVE_DETAIL_LIST) {
      /*
       * Parser has marked this as a detailed view, which
       * means that we just had to display the details
       * of the specified archive NAME.
       *
       * NOTE: In this case we usually don't
       * expect multiple results, however, the code supports this
       * at this point nevertheless, since we just call
       * getArchiveList with the affected NAME property attached only.
       */

      /*
       * Get a filtered list for the specified properties.
       */
      auto archiveList = catalog->getArchiveList(make_shared<CatalogDescr>(*this),
                                                 this->affectedAttributes);

      /* Print headline */
      cout << CPGBackupCtlBase::makeHeader("Detail view for archive",
                                           boost::format("%-20s\t%-30s") % "Property" % "Setting",
                                           80);

      for (auto& descr: *archiveList) {
        cout << boost::format("%-20s\t%-30s") % "NAME" % descr->archive_name << endl;
        cout << boost::format("%-20s\t%-30s") % "DIRECTORY" % descr->directory << endl;
        cout << boost::format("%-20s\t%-30s") % "PGHOST" % descr->coninfo->pghost << endl;
        cout << boost::format("%-20s\t%-30d") % "PGPORT" % descr->coninfo->pgport << endl;
        cout << boost::format("%-20s\t%-30s") % "PGDATABASE" % descr->coninfo->pgdatabase << endl;
        cout << boost::format("%-20s\t%-30s") % "PGUSER" % descr->coninfo->pguser << endl;
        cout << boost::format("%-20s\t%-30s") % "DSN" % descr->coninfo->dsn << endl;
        cout << boost::format("%-20s\t%-30s") % "COMPRESSION" % descr->compression << endl;
        cout << CPGBackupCtlBase::makeLine(80) << endl;
      }
    }

  } catch (CPGBackupCtlFailure& e) {
    /* rollback transaction */
    catalog->rollbackTransaction();
    throw e;
  }

  catalog->close();

}

void ListArchiveCatalogCommand::setOutputMode(ListArchiveOutputMode mode) {
  this->mode = mode;
}

AlterArchiveCatalogCommand::AlterArchiveCatalogCommand() {
  this->tag = ALTER_ARCHIVE;
}

AlterArchiveCatalogCommand::AlterArchiveCatalogCommand(std::shared_ptr<CatalogDescr> descr) {

  this->copy(*(descr.get()));
}

AlterArchiveCatalogCommand::AlterArchiveCatalogCommand(std::shared_ptr<BackupCatalog> catalog) {
  this->tag = ALTER_ARCHIVE;
  this->catalog = catalog;
}

void AlterArchiveCatalogCommand::execute(bool ignoreMissing) {

  shared_ptr<CatalogDescr> temp_descr(nullptr);

  /*
   * Die hard in case no catalog available.
   */
  if (this->catalog == nullptr)
    throw CArchiveIssue("could not execute archive command: no catalog");

  /*
   * Open the catalog, if not yet done.
   */
  if (!this->catalog->available()) {
    this->catalog->open_rw();
  }

  catalog->startTransaction();

  try {

    /*
     * Check if the specified archive name really exists.
     * If false and ignoreMissing is false, error out. Otherwise
     * proceed, which turns this action into a no-op, effectively.
     */
    temp_descr = this->catalog->existsByName(this->archive_name);
    if (temp_descr->id >= 0) {

      this->id = temp_descr->id;
      this->catalog->updateArchiveAttributes(make_shared<CatalogDescr>(*this),
                                             this->affectedAttributes);
    } else {

      if (!ignoreMissing) {
        ostringstream oss;
        oss << "could not alter archive: archive name \"" << this->archive_name << "\" does not exists";
        throw CArchiveIssue(oss.str());
      }

    }

    this->catalog->commitTransaction();

  } catch(CPGBackupCtlFailure& e) {
    this->catalog->rollbackTransaction();
    throw e;
  }

}

DropArchiveCatalogCommand::DropArchiveCatalogCommand(std::shared_ptr<CatalogDescr> descr) {

  this->copy(*(descr.get()));
}

DropArchiveCatalogCommand::DropArchiveCatalogCommand(std::shared_ptr<BackupCatalog> catalog) {
  this->catalog = catalog;
  this->tag     = DROP_ARCHIVE;
}

DropArchiveCatalogCommand::DropArchiveCatalogCommand() {
  this->tag = DROP_ARCHIVE;
}

void DropArchiveCatalogCommand::execute(bool existsOk) {

  shared_ptr<CatalogDescr> temp_descr(nullptr);

  /*
   * Die hard in case no catalog available.
   */
  if (this->catalog == nullptr)
    throw CArchiveIssue("could not execute archive command: no catalog");

  /*
   * Open the catalog, if not yet done.
   */
  if (!this->catalog->available()) {
    this->catalog->open_rw();
  }

  catalog->startTransaction();

  try {
    /*
     * existsOk means in this case we check wether
     * the archive exists. If false, raise an error, otherwise
     * just pass which turns this method into a no-op.
     */
    temp_descr = catalog->existsByName(this->archive_name);

    if (temp_descr->id >= 0) {

      /* archive name exists, so drop it... */
      catalog->dropArchive(this->archive_name);

    } else {

      if (!existsOk) {
        ostringstream oss;
        oss << "specified archive name \"" << this->archive_name << "\" does not exists";

        /*
         * Note: cleanup handled below in exception handler.
         */
        throw CArchiveIssue(oss.str());
      }
    }

    catalog->dropArchive(this->archive_name);
    catalog->commitTransaction();

  } catch (CPGBackupCtlFailure& e) {

    /*
     * handle error condition, but don't suppress the
     * exception from the caller
     */
    catalog->rollbackTransaction();
    throw e;

  }

}

CreateArchiveCatalogCommand::CreateArchiveCatalogCommand() {
  this->tag = CREATE_ARCHIVE;
}

CreateArchiveCatalogCommand::CreateArchiveCatalogCommand(std::shared_ptr<CatalogDescr> descr) {

  this->copy(*(descr.get()));

}

std::shared_ptr<BackupCatalog> BaseCatalogCommand::getCatalog() {
  return this->catalog;
}

void BaseCatalogCommand::setCatalog(std::shared_ptr<BackupCatalog> catalog) {
  this->catalog = catalog;
}

CreateArchiveCatalogCommand::CreateArchiveCatalogCommand(shared_ptr<BackupCatalog> catalog) {

  this->catalog = catalog;
  this->tag = CREATE_ARCHIVE;

}

void CreateArchiveCatalogCommand::execute(bool existsOk) {

  shared_ptr<CatalogDescr> temp_descr(nullptr);

  /*
   * Die hard in case no catalog available.
   */
  if (this->catalog == NULL)
    throw CArchiveIssue("could not execute archive command: no catalog");

  /*
   * Open the catalog, if not yet done
   */
  if (!this->catalog->available()) {
    this->catalog->open_rw();
  }

  this->catalog->startTransaction();

  /*
   * If existsOk is TRUE and we have an
   * existing archive entry in the catalog, proceed.
   * Otherwise we die a hard and unpleasent immediate
   * death...
   */
  temp_descr = this->catalog->exists(this->directory);

  try {
    if (temp_descr->id < 0) {

      /*
       * This is a new archive entry.
       */
      this->catalog->createArchive((temp_descr = make_shared<CatalogDescr>(*this)));

      /*
       * Create the corresponding database connection entry.
       *
       * NOTE: createArchive() should have set the archive id
       *       in the catalog descriptor if successful.
       */
      temp_descr->setConnectionType(ConnectionDescr::CONNECTION_TYPE_BASEBACKUP);
      this->catalog->createCatalogConnection(temp_descr->coninfo);

      this->catalog->commitTransaction();

    }
    else {

      if (!existsOk) {
        ostringstream oss;
        oss << "archive already exists: \"" << this->directory << "\"";
        throw CArchiveIssue(oss.str());
      }

      /*
       * ID of the existing catalog entry needed.
       */
      this->id = temp_descr->id;
      this->catalog->startTransaction();

      /*
       * Update the existing archive catalog entry.
       */
      this->catalog->updateArchiveAttributes(make_shared<CatalogDescr>(*this),
                                             this->affectedAttributes);

      /*
       * ... and we're done.
       */
      this->catalog->commitTransaction();
    }
  } catch(CPGBackupCtlFailure& e) {
    this->catalog->rollbackTransaction();
    /* re-throw ... */
    throw e;
  }

}

BackgroundWorkerCommandHandle::BackgroundWorkerCommandHandle(std::shared_ptr<BackupCatalog> catalog) {
  this->tag = BACKGROUND_WORKER_COMMAND;
  this->subTag = EMPTY_DESCR;
  this->catalog = catalog;
}

BackgroundWorkerCommandHandle::BackgroundWorkerCommandHandle(std::shared_ptr<CatalogDescr> descr) {

  this->copy(*(descr.get()));

  /*
   * Rewrite identity command tag, subTag transports
   * the encapsulated catalog command.
   */
  this->subTag = this->tag;
  this->tag = BACKGROUND_WORKER_COMMAND;

}

void BackgroundWorkerCommandHandle::execute(bool noop) {

}
