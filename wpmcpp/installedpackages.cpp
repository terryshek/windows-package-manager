#include "installedpackages.h"

#include <windows.h>
#include <msi.h>

#include <QApplication>
#include <QDebug>

#include "windowsregistry.h"
#include "package.h"
#include "version.h"
#include "packageversion.h"
#include "repository.h"
#include "wpmutils.h"
#include "controlpanelthirdpartypm.h"
#include "msithirdpartypm.h"
#include "wellknownprogramsthirdpartypm.h"
#include "hrtimer.h"
#include "installedpackagesthirdpartypm.h"
#include "dbrepository.h"

InstalledPackages InstalledPackages::def;

static bool installedPackageVersionLessThan(const InstalledPackageVersion* a,
        const InstalledPackageVersion* b)
{
    int r = a->package.compare(b->package);
    if (r == 0) {
        r = a->version.compare(b->version);
    }

    return r > 0;
}

InstalledPackages* InstalledPackages::getDefault()
{
    return &def;
}

InstalledPackages::InstalledPackages()
{
}

InstalledPackageVersion* InstalledPackages::find(const QString& package,
        const Version& version) const
{
    return this->data.value(PackageVersion::getStringId(package, version));
}

void InstalledPackages::detect3rdParty(AbstractThirdPartyPM *pm, bool replace)
{
    Repository rep;
    QList<InstalledPackageVersion*> installed;
    pm->scan(&installed, &rep);

    // TODO: handle error
    Job* job = new Job();
    DBRepository::getDefault()->saveAll(job, &rep, replace);
    delete job;

    AbstractRepository* r = AbstractRepository::getDefault_();
    /*
    for (int i = 0; i < rep.packages.count(); i++) {
        Package* p = rep.packages.at(i);
        Package* fp = r->findPackage_(p->name);
        if (fp)
            delete fp;
        else
            r->savePackage(p); // TODO: handle error
    }
    for (int i = 0; i < rep.packageVersions.count(); i++) {
        PackageVersion* pv = rep.packageVersions.at(i);
        PackageVersion* fpv = r->findPackageVersion_(pv->package, pv->version);
        if (fpv)
            delete fpv;
        else
            r->savePackageVersion(pv); // TODO: handle error

        Package* p = r->findPackage_(pv->package);
        if (!p) {
            p = new Package(pv->package, pv->package);
            r->savePackage(p);
        }
        delete p;
    }
    */

    QStringList packagePaths = this->getAllInstalledPackagePaths();
    QDir d;
    for (int i = 0; i < installed.count(); i++) {
        InstalledPackageVersion* ipv = installed.at(i);
        QString err; // TODO: handle error
        QScopedPointer<PackageVersion> pv(
                r->findPackageVersion_(ipv->package, ipv->version, &err));
        if (!pv)
            continue;

        QString path = getPath(ipv->package, ipv->version);
        if (!path.isEmpty()) {
            continue;
        }

        PackageVersionFile* u = pv->findFile(".Npackd\\Uninstall.bat");
        if (!u)
            continue;

        path = ipv->directory;

        if (!path.isEmpty()) {
            path = WPMUtils::normalizePath(path);
            if (WPMUtils::isUnderOrEquals(path, packagePaths))
                continue;
        }

        if (path.isEmpty()) {
            Package* p = r->findPackage_(ipv->package);

            // TODO: remove
            /* if (!p)
                WPMUtils::outputTextConsole("Cannot find package for " +
                        ipv->package + " " +
                        ipv->version.getVersionString() + "\n");
                        */

            path = WPMUtils::getInstallationDirectory() +
                    "\\NpackdDetected\\" +
            WPMUtils::makeValidFilename(p->title, '_');
            if (d.exists(path)) {
                path = WPMUtils::findNonExistingFile(path + "-" +
                        ipv->version.getVersionString(), "");
            }
            d.mkpath(path);
            delete p;
        }
        if (d.exists(path)) {
            if (d.mkpath(path + "\\.Npackd")) {
                QFile file(path + "\\.Npackd\\Uninstall.bat");
                if (file.open(QIODevice::WriteOnly |
                        QIODevice::Truncate)) {
                    QTextStream stream(&file);
                    stream.setCodec("UTF-8");
                    stream << u->content;
                    file.close();

                    //qDebug() << "InstalledPackages::detectOneControlPanelProgram "
                    //        "setting path for " << pv->toString() << " to" << dir;
                    setPackageVersionPath(ipv->package,
                            ipv->version, path);
                }
            }
        }
    }
    qDeleteAll(installed);

    /* TODO:
*/
    /* TODO:
    // remove uninstalled packages
    QMapIterator<QString, InstalledPackageVersion*> i(data);
    while (i.hasNext()) {
        i.next();
        InstalledPackageVersion* ipv = i.value();
        if (ipv->detectionInfo.indexOf("control-panel:") == 0 &&
                ipv->installed() &&
                !foundDetectionInfos.contains(ipv->detectionInfo)) {
            qDebug() << "control-panel package removed: " << ipv->package;
            ipv->setPath("");
        }
    }
    */
}

InstalledPackageVersion* InstalledPackages::findOrCreate(const QString& package,
        const Version& version)
{
    QString key = PackageVersion::getStringId(package, version);
    InstalledPackageVersion* r = this->data.value(key);
    if (!r) {
        r = new InstalledPackageVersion(package, version, "");
        this->data.insert(key, r);

        // qDebug() << "InstalledPackages::findOrCreate " << package;
        // TODO: error is not handled
        saveToRegistry(r);
        fireStatusChanged(package, version);
    }
    return r;
}

QString InstalledPackages::setPackageVersionPath(const QString& package,
        const Version& version,
        const QString& directory)
{
    QString err;

    InstalledPackageVersion* ipv = this->find(package, version);
    if (!ipv) {
        ipv = new InstalledPackageVersion(package, version, directory);
        this->data.insert(package + "/" + version.getVersionString(), ipv);
        err = saveToRegistry(ipv);
        fireStatusChanged(package, version);
    } else {
        ipv->setPath(directory);
        err = saveToRegistry(ipv);
        fireStatusChanged(package, version);
    }

    return err;
}

void InstalledPackages::setPackageVersionPathIfNotInstalled(
        const QString& package,
        const Version& version,
        const QString& directory)
{
    InstalledPackageVersion* ipv = findOrCreate(package, version);
    if (!ipv->installed()) {
        ipv->setPath(directory);

        // TODO: error is ignored
        saveToRegistry(ipv);
        fireStatusChanged(ipv->package, ipv->version);
    }
}

QList<InstalledPackageVersion*> InstalledPackages::getAll() const
{
    QList<InstalledPackageVersion*> all = this->data.values();
    QList<InstalledPackageVersion*> r;
    for (int i = 0; i < all.count(); i++) {
        InstalledPackageVersion* ipv = all.at(i);
        if (ipv->installed())
            r.append(ipv->clone());
    }
    return r;
}

QList<InstalledPackageVersion *> InstalledPackages::getByPackage(
        const QString &package) const
{
    QList<InstalledPackageVersion*> all = this->data.values();
    QList<InstalledPackageVersion*> r;
    for (int i = 0; i < all.count(); i++) {
        InstalledPackageVersion* ipv = all.at(i);
        if (ipv->installed() && ipv->package == package)
            r.append(ipv->clone());
    }
    return r;
}

QStringList InstalledPackages::getAllInstalledPackagePaths() const
{
    QStringList r;
    QList<InstalledPackageVersion*> ipvs = this->data.values();
    for (int i = 0; i < ipvs.count(); i++) {
        InstalledPackageVersion* ipv = ipvs.at(i);
        if (ipv->installed())
            r.append(ipv->getDirectory());
    }
    return r;
}

void InstalledPackages::refresh(Job *job)
{
    HRTimer timer(5);
    /*
     * Example:
     *  0 :  0  ms
        1 :  0  ms
        2 :  207  ms
        3 :  23015  ms
        4 :  1116  ms
     */
    timer.time(0);

    if (!job->isCancelled() && job->getErrorMessage().isEmpty()) {
        job->setHint(QApplication::tr("Detecting directories deleted externally"));
        QList<InstalledPackageVersion*> ipvs = this->data.values();
        for (int i = 0; i < ipvs.count(); i++) {
            InstalledPackageVersion* ipv = ipvs.at(i);
            if (ipv->installed()) {
                QDir d(ipv->getDirectory());
                d.refresh();
                if (!d.exists()) {
                    ipv->directory = "";
                    // TODO: error message is not handled
                    saveToRegistry(ipv);
                    fireStatusChanged(ipv->package, ipv->version);
                }
            }
        }
        job->setProgress(0.2);
    }
    timer.time(1);

    if (!job->isCancelled() && job->getErrorMessage().isEmpty()) {
        job->setHint(QApplication::tr("Reading registry package database"));
        readRegistryDatabase();
        job->setProgress(0.5);
    }
    timer.time(2);

    if (!job->isCancelled() && job->getErrorMessage().isEmpty()) {
        job->setHint(QApplication::tr("Detecting software"));
        Job* d = job->newSubJob(0.2);

        HRTimer timer2(5);
        /*
         * Example:
            0 :  0  ms
            1 :  1095  ms
            2 :  123  ms
            3 :  567  ms
            4 :  487  ms
         */
        timer2.time(0);
        /* TODO
        job->setHint("Updating NPACKD_CL");
        AbstractRepository* rep = AbstractRepository::getDefault_();
        rep->updateNpackdCLEnvVar();
        job->setProgress(1);
        */
        AbstractThirdPartyPM* pm = new InstalledPackagesThirdPartyPM();
        detect3rdParty(pm, false);
        delete pm;
        timer2.time(1);

        pm = new WellKnownProgramsThirdPartyPM();
        detect3rdParty(pm, false);
        delete pm;
        timer2.time(2);

        /* TODO: ???
        // MSI package detection should happen before the detection for
        // control panel programs
        pm = new MSIThirdPartyPM();
        detect3rdParty(pm, true);
        delete pm;
        timer2.time(3);

        pm = new ControlPanelThirdPartyPM();
        detect3rdParty(pm, true);
        delete pm;
        timer2.time(4);
        */

        //timer2.dump();

        delete d;
    }
    timer.time(3);

    if (job->shouldProceed(QApplication::tr("Clearing information about installed package versions in nested directories"))) {
        clearPackagesInNestedDirectories();
        job->setProgress(1);
    }
    timer.time(4);
    // timer.dump();

    job->complete();
}

QString InstalledPackages::getPath(const QString &package,
        const Version &version) const
{
    QString r;
    InstalledPackageVersion* ipv = find(package, version);
    if (ipv)
        r = ipv->getDirectory();

    return r;
}

bool InstalledPackages::isInstalled(const QString &package,
        const Version &version) const
{
    InstalledPackageVersion* ipv = find(package, version);
    return ipv && ipv->installed();
}

void InstalledPackages::fireStatusChanged(const QString &package,
        const Version &version)
{
    emit statusChanged(package, version);
}

void InstalledPackages::clearPackagesInNestedDirectories() {
    QList<InstalledPackageVersion*> pvs = this->getAll();
    qSort(pvs.begin(), pvs.end(), installedPackageVersionLessThan);

    for (int j = 0; j < pvs.count(); j++) {
        InstalledPackageVersion* pv = pvs.at(j);
        if (pv->installed() && !WPMUtils::pathEquals(pv->getDirectory(),
                WPMUtils::getWindowsDir())) {
            for (int i = j + 1; i < pvs.count(); i++) {
                InstalledPackageVersion* pv2 = pvs.at(i);
                if (pv2->installed() && !WPMUtils::pathEquals(pv2->getDirectory(),
                        WPMUtils::getWindowsDir())) {
                    if (WPMUtils::isUnder(pv2->getDirectory(),
                            pv->getDirectory()) ||
                            WPMUtils::pathEquals(pv2->getDirectory(),
                                    pv->getDirectory())) {
                        pv2->setPath("");

                        // TODO: error message is ignored
                        saveToRegistry(pv2);
                        fireStatusChanged(pv2->package, pv2->version);
                    }
                }
            }
        }
    }
}

void InstalledPackages::readRegistryDatabase()
{
    // TODO: return error message?
    this->data.clear();

    QString err;
    WindowsRegistry packagesWR;
    err = packagesWR.open(HKEY_LOCAL_MACHINE,
            "SOFTWARE\\Npackd\\Npackd\\Packages", false, KEY_READ);

    if (err.isEmpty()) {
        QStringList entries = packagesWR.list(&err);
        for (int i = 0; i < entries.count(); ++i) {
            QString name = entries.at(i);
            int pos = name.lastIndexOf("-");
            if (pos <= 0)
                continue;

            QString packageName = name.left(pos);
            if (!Package::isValidName(packageName))
                continue;

            QString versionName = name.right(name.length() - pos - 1);
            Version version;
            if (!version.setVersion(versionName))
                continue;

            WindowsRegistry entryWR;
            err = entryWR.open(packagesWR, name, KEY_READ);
            if (!err.isEmpty())
                continue;

            QString p = entryWR.get("Path", &err).trimmed();
            if (!err.isEmpty())
                continue;

            QString dir;
            if (p.isEmpty())
                dir = "";
            else {
                QDir d(p);
                if (d.exists()) {
                    dir = p;
                } else {
                    dir = "";
                }
            }

            if (dir.isEmpty())
                continue;

            InstalledPackageVersion* ipv = new InstalledPackageVersion(
                    packageName, version, dir);
            ipv->detectionInfo = entryWR.get("DetectionInfo", &err);
            if (!err.isEmpty()) {
                // ignore
                ipv->detectionInfo = "";
                err = "";
            }

            if (!ipv->directory.isEmpty()) {
                /*
                qDebug() << "adding " << ipv->package <<
                        ipv->version.getVersionString() << "in" <<
                        ipv->directory;*/
                this->data.insert(PackageVersion::getStringId(
                        packageName, version), ipv);

                fireStatusChanged(packageName, version);
            } else {
                delete ipv;
            }
        }
    }
}

QString InstalledPackages::findPath_npackdcl(const Dependency& dep)
{
    QString ret;

    QString err;
    WindowsRegistry packagesWR;
    err = packagesWR.open(HKEY_LOCAL_MACHINE,
            "SOFTWARE\\Npackd\\Npackd\\Packages", false, KEY_READ);

    if (err.isEmpty()) {
        Version found = Version::EMPTY;

        QStringList entries = packagesWR.list(&err);
        for (int i = 0; i < entries.count(); ++i) {
            QString name = entries.at(i);
            int pos = name.lastIndexOf("-");
            if (pos <= 0)
                continue;

            QString packageName = name.left(pos);
            if (packageName != dep.package)
                continue;

            QString versionName = name.right(name.length() - pos - 1);
            Version version;
            if (!version.setVersion(versionName))
                continue;

            if (!dep.test(version))
                continue;

            if (found != Version::EMPTY) {
                if (version.compare(found) < 0)
                    continue;
            }

            WindowsRegistry entryWR;
            err = entryWR.open(packagesWR, name, KEY_READ);
            if (!err.isEmpty())
                continue;

            QString p = entryWR.get("Path", &err).trimmed();
            if (!err.isEmpty())
                continue;

            QString dir;
            if (p.isEmpty())
                dir = "";
            else {
                QDir d(p);
                if (d.exists()) {
                    dir = p;
                } else {
                    dir = "";
                }
            }

            if (dir.isEmpty())
                continue;

            found = version;
            ret = dir;
        }
    }

    return ret;
}

QString InstalledPackages::saveToRegistry(InstalledPackageVersion *ipv)
{
    WindowsRegistry machineWR(HKEY_LOCAL_MACHINE, false);
    QString r;
    QString keyName = "SOFTWARE\\Npackd\\Npackd\\Packages";
    QString pn = ipv->package + "-" + ipv->version.getVersionString();

    // TODO: remove
    WPMUtils::outputTextConsole(
            "InstalledPackages::saveToRegistry " + ipv->directory + " " +
            ipv->package + " " + ipv->version.getVersionString() + "\n");

    if (!ipv->directory.isEmpty()) {
        WindowsRegistry wr = machineWR.createSubKey(keyName + "\\" + pn, &r);
        if (r.isEmpty()) {
            r = wr.set("Path", ipv->directory);
            if (r.isEmpty())
                r = wr.set("DetectionInfo", ipv->detectionInfo);

            // for compatibility with Npackd 1.16 and earlier. They
            // see all package versions by default as "externally installed"
            if (r.isEmpty())
                r = wr.setDWORD("External", 0);
        }
    } else {
        // qDebug() << "deleting " << pn;
        WindowsRegistry packages;
        r = packages.open(machineWR, keyName, KEY_ALL_ACCESS);
        if (r.isEmpty()) {
            r = packages.remove(pn);
        }
    }
    //qDebug() << "InstalledPackageVersion::save " << pn << " " <<
    //        this->directory;
    return r;
}

