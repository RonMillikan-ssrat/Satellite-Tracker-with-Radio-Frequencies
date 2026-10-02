#ifndef CATALOG_H
#define CATALOG_H

#include <QString>
#include <QStringList>
#include <QList>

// A named set of satellites the user can load. Each catalog is built from one
// or more CelesTrak GP queries, optionally filtered by SATCAT owner code.
struct SatelliteCatalog {
    QString id;            // stable key used in settings, the API and the MCP tools
    QString label;         // shown in the GUI
    QString description;   // tooltip / API description
    QStringList gpQueries; // appended to gp.php, e.g. "GROUP=amateur" (results are merged)
    QStringList owners;    // SATCAT OWNER codes to keep (e.g. "CIS", "PRC"); empty = keep all

    static const QList<SatelliteCatalog>& all();
    static const SatelliteCatalog* find(const QString& id);
    static QString defaultId() { return "amateur"; }

    static QString gpUrl(const QString& query);
    static QString satcatUrl();          // active-satellite SATCAT (CSV), used for owner filters
    static QString satcatFallbackUrl();  // full SATCAT CSV, used if the first one fails
};

#endif // CATALOG_H
